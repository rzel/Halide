#include "ScheduleFunctions.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "ExprUsesVar.h"
#include "Var.h"
#include "Qualify.h"
#include "IRMutator.h"
#include "Target.h"
#include "Inline.h"
#include "CodeGen_GPU_Dev.h"
#include "IRPrinter.h"
#include "Func.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;
using std::make_pair;
using std::set;

namespace {
// A structure representing a containing LetStmt, IfThenElse, or For
// loop. Used in build_provide_loop_nest below.
struct Container {
    enum Type {For, Let, If};
    Type type;
    // If it's a for loop, the index in the dims list.
    int dim_idx;
    string name;
    Expr value;
};

bool var_name_match(string candidate, string var) {
    internal_assert(var.find('.') == string::npos)
        << "var_name_match expects unqualified names for the second argument. "
        << "Name passed: " << var << "\n";
    if (candidate == var) return true;
    return Internal::ends_with(candidate, "." + var);
}
}

class ContainsImpureCall : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) {
        if (!op->is_pure()) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool result = false;
    ContainsImpureCall() {}
};

bool contains_impure_call(const Expr &expr) {
    ContainsImpureCall is_not_pure;
    expr.accept(&is_not_pure);
    return is_not_pure.result;
}

// Build a loop nest about a provide node using a schedule
Stmt build_provide_loop_nest_helper(string func_name,
                                    string prefix,
                                    int start_fuse, // Fuse the dims starting from start_fuse to outermost (if not -1)
                                    const vector<string> &dims, // The dims of the initial definition
                                    const vector<Expr> &site,
                                    const vector<Expr> &values,
                                    const vector<Expr> &predicates,
                                    const Schedule &s,
                                    bool is_update) {
    // We'll build it from inside out, starting from a store node,
    // then wrapping it in for loops.

    // Make the (multi-dimensional multi-valued) store node.
    Stmt stmt = Provide::make(func_name, values, site);

    // Add appopriate predicates on the fused loop vars to ensure we don't
    // go out of bounds. Ignore the __outermost dims since it's going to be
    // removed later anyway.
    for (int i = start_fuse; (i >= 0) && (i < (int)s.dims().size()-1); ++i) {
        const Dim &dim = s.dims()[i];
        Expr var = Variable::make(Int(32), prefix + dim.var);
        Expr max = Variable::make(Int(32), prefix + dim.var + ".loop_max");
        Expr min = Variable::make(Int(32), prefix + dim.var + ".loop_min");
        stmt = IfThenElse::make(likely(min <= var), stmt);
        stmt = IfThenElse::make(likely(var <= max), stmt);
    }

    // A map of the dimensions for which we know the extent is a
    // multiple of some Expr. This can happen due to a bound, or
    // align_bounds directive, or if a dim comes from the inside
    // of a split.
    map<string, Expr> dim_extent_alignment;

    // First hunt through the bounds for them.
    for (const Bound &i : s.bounds()) {
        if (i.extent.defined()) {
            dim_extent_alignment[i.var] = i.extent;
        }
        if (i.modulus.defined()) {
            dim_extent_alignment[i.var] = i.modulus;
        }
    }
    // Then use any reduction domain.
    for (const ReductionVariable &i : s.rvars()) {
        dim_extent_alignment[i.var] = i.extent;
    }

    vector<Split> splits = s.splits();

    // Define the function args in terms of the loop variables using the splits
    for (const Split &split : splits) {
        Expr outer = Variable::make(Int(32), prefix + split.outer);
        Expr outer_max = Variable::make(Int(32), prefix + split.outer + ".loop_max");
        if (split.is_split()) {
            Expr inner = Variable::make(Int(32), prefix + split.inner);
            Expr old_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
            Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
            Expr old_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");

            dim_extent_alignment[split.inner] = split.factor;

            Expr base = outer * split.factor + old_min;
            string base_name = prefix + split.inner + ".base";
            Expr base_var = Variable::make(Int(32), base_name);
            string old_var_name = prefix + split.old_var;
            Expr old_var = Variable::make(Int(32), old_var_name);

            map<string, Expr>::iterator iter = dim_extent_alignment.find(split.old_var);

            if (is_update) {
                user_assert(split.tail != TailStrategy::ShiftInwards)
                    << "When splitting Var " << split.old_var
                    << " ShiftInwards is not a legal tail strategy for update definitions, as"
                    << " it may change the meaning of the algorithm\n";
            }

            if (split.exact) {
                user_assert(split.tail == TailStrategy::Auto ||
                            split.tail == TailStrategy::GuardWithIf)
                    << "When splitting Var " << split.old_var
                    << " the tail strategy must be GuardWithIf or Auto. "
                    << "Anything else may change the meaning of the algorithm\n";
            }

            TailStrategy tail = split.tail;
            if (tail == TailStrategy::Auto) {
                if (split.exact) {
                    tail = TailStrategy::GuardWithIf;
                } else if (is_update) {
                    tail = TailStrategy::RoundUp;
                } else {
                    tail = TailStrategy::ShiftInwards;
                }
            }

            if ((iter != dim_extent_alignment.end()) &&
                is_zero(simplify(iter->second % split.factor))) {
                // We have proved that the split factor divides the
                // old extent. No need to adjust the base or add an if
                // statement.
                dim_extent_alignment[split.outer] = iter->second / split.factor;
            } else if (is_negative_const(split.factor) || is_zero(split.factor)) {
                user_error << "Can't split " << split.old_var << " by " << split.factor
                           << ". Split factors must be strictly positive\n";
            } else if (is_one(split.factor)) {
                // The split factor trivially divides the old extent,
                // but we know nothing new about the outer dimension.
            } else if (tail == TailStrategy::GuardWithIf) {
                // It's an exact split but we failed to prove that the
                // extent divides the factor. Use predication.

                // Make a var representing the original var minus its
                // min. It's important that this is a single Var so
                // that bounds inference has a chance of understanding
                // what it means for it to be limited by the if
                // statement's condition.
                Expr rebased = outer * split.factor + inner;
                string rebased_var_name = prefix + split.old_var + ".rebased";
                Expr rebased_var = Variable::make(Int(32), rebased_var_name);
                stmt = substitute(prefix + split.old_var, rebased_var + old_min, stmt);

                // Tell Halide to optimize for the case in which this
                // condition is true by partitioning some outer loop.
                Expr cond = likely(rebased_var < old_extent);
                stmt = IfThenElse::make(cond, stmt, Stmt());
                stmt = LetStmt::make(rebased_var_name, rebased, stmt);

            } else if (tail == TailStrategy::ShiftInwards) {
                // Adjust the base downwards to not compute off the
                // end of the realization.

                // We'll only mark the base as likely (triggering a loop
                // partition) if we're at or inside the innermost
                // non-trivial loop.
                base = likely_if_innermost(base);

                base = Min::make(base, old_max + (1 - split.factor));
            } else {
                internal_assert(tail == TailStrategy::RoundUp);
            }

            // Substitute in the new expression for the split variable ...
            stmt = substitute(old_var_name, base_var + inner, stmt);
            // ... but also define it as a let for the benefit of bounds inference.
            stmt = LetStmt::make(old_var_name, base_var + inner, stmt);
            stmt = LetStmt::make(base_name, base, stmt);

        } else if (split.is_fuse()) {
            // Define the inner and outer in terms of the fused var
            Expr fused = Variable::make(Int(32), prefix + split.old_var);
            Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
            Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");
            Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");

            // If the inner extent is zero, the loop will never be
            // entered, but the bounds expressions lifted out might
            // contain divides or mods by zero. In the cases where
            // simplification of inner and outer matter, inner_extent
            // is a constant, so the max will simplify away.
            Expr factor = max(inner_extent, 1);
            Expr inner = fused % factor + inner_min;
            Expr outer = fused / factor + outer_min;

            stmt = substitute(prefix + split.inner, inner, stmt);
            stmt = substitute(prefix + split.outer, outer, stmt);
            stmt = LetStmt::make(prefix + split.inner, inner, stmt);
            stmt = LetStmt::make(prefix + split.outer, outer, stmt);

            // Maintain the known size of the fused dim if
            // possible. This is important for possible later splits.
            map<string, Expr>::iterator inner_dim = dim_extent_alignment.find(split.inner);
            map<string, Expr>::iterator outer_dim = dim_extent_alignment.find(split.outer);
            if (inner_dim != dim_extent_alignment.end() &&
                outer_dim != dim_extent_alignment.end()) {
                dim_extent_alignment[split.old_var] = inner_dim->second*outer_dim->second;
            }
        } else {
            // rename or purify
            stmt = substitute(prefix + split.old_var, outer, stmt);
            stmt = LetStmt::make(prefix + split.old_var, outer, stmt);
        }
    }

    // All containing lets and fors. Outermost first.
    vector<Container> nest;

    // Put the desired loop nest into the containers vector.
    for (int i = (int)s.dims().size() - 1; i >= 0; i--) {
        const Dim &dim = s.dims()[i];
        Container c = {Container::For, i, prefix + dim.var, Expr()};
        nest.push_back(c);
    }

    // Strip off the lets into the containers vector.
    while (const LetStmt *let = stmt.as<LetStmt>()) {
        Container c = {Container::Let, 0, let->name, let->value};
        nest.push_back(c);
        stmt = let->body;
    }

    // Put all the reduction domain predicate into the containers vector.
    int n_predicates = predicates.size();
    for (Expr pred : predicates) {
        pred = qualify(prefix, pred);
        Container c = {Container::If, 0, "", pred};
        nest.push_back(c);
    }

    // Resort the containers vector so that lets are as far outwards
    // as possible. Use reverse insertion sort. Start at the first letstmt.
    for (int i = (int)s.dims().size(); i < (int)nest.size() - n_predicates; i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::Let);

        for (int j = i-1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j+1].value.defined());
            if (!expr_uses_var(nest[j+1].value, nest[j].name)) {
                std::swap(nest[j+1], nest[j]);
            } else {
                break;
            }
        }
    }

    // Sort the predicate guards so they are as far outwards as possible.
    for (int i = (int)nest.size() - n_predicates; i < (int)nest.size(); i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::If);

        // Cannot lift out the predicate guard if it contains call to non-pure function
        if (contains_impure_call(nest[i].value)) {
            continue;
        }

        for (int j = i-1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j+1].value.defined());
            if (!expr_uses_var(nest[j+1].value, nest[j].name)) {
                std::swap(nest[j+1], nest[j]);
            } else {
                break;
            }
        }
    }

    // Rewrap the statement in the containing lets and fors.
    for (int i = (int)nest.size() - 1; i >= 0; i--) {
        if (nest[i].type == Container::Let) {
            internal_assert(nest[i].value.defined());
            stmt = LetStmt::make(nest[i].name, nest[i].value, stmt);
        } else if (nest[i].type == Container::If) {
            internal_assert(nest[i].value.defined());
            stmt = IfThenElse::make(likely(nest[i].value), stmt, Stmt());
        } else {
            internal_assert(nest[i].type == Container::For);
            const Dim &dim = s.dims()[nest[i].dim_idx];
            Expr min = Variable::make(Int(32), nest[i].name + ".loop_min");
            Expr extent = Variable::make(Int(32), nest[i].name + ".loop_extent");
            stmt = For::make(nest[i].name, min, extent, dim.for_type, dim.device_api, stmt);
        }
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args. If it is a purify, we should use the bounds
    // from the dims instead.
    for (size_t i = splits.size(); i > 0; i--) {
        const Split &split = splits[i-1];
        Expr old_var_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");
        Expr old_var_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
        Expr old_var_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
        if (split.is_split()) {
            Expr inner_extent = split.factor;
            Expr outer_extent = (old_var_max - old_var_min + split.factor)/split.factor;
            stmt = LetStmt::make(prefix + split.inner + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.inner + ".loop_max", inner_extent-1, stmt);
            stmt = LetStmt::make(prefix + split.inner + ".loop_extent", inner_extent, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_max", outer_extent-1, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_extent", outer_extent, stmt);
        } else if (split.is_fuse()) {
            // Define bounds on the fused var using the bounds on the inner and outer
            Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");
            Expr outer_extent = Variable::make(Int(32), prefix + split.outer + ".loop_extent");
            Expr fused_extent = inner_extent * outer_extent;
            stmt = LetStmt::make(prefix + split.old_var + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.old_var + ".loop_max", fused_extent - 1, stmt);
            stmt = LetStmt::make(prefix + split.old_var + ".loop_extent", fused_extent, stmt);
        } else if (split.is_rename()) {
            stmt = LetStmt::make(prefix + split.outer + ".loop_min", old_var_min, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_max", old_var_max, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_extent", old_var_extent, stmt);
        }
        // Do nothing for purify
    }

    // Define the bounds on the outermost dummy dimension.
    {
        string o = prefix + Var::outermost().name();
        stmt = LetStmt::make(o + ".loop_min", 0, stmt);
        stmt = LetStmt::make(o + ".loop_max", 0, stmt);
        stmt = LetStmt::make(o + ".loop_extent", 1, stmt);
    }

    // Define the loop mins and extents in terms of the mins and maxs produced by bounds inference
    for (const std::string &i : dims) {
        string var = prefix + i;
        Expr max = Variable::make(Int(32), var + ".max");
        Expr min = Variable::make(Int(32), var + ".min"); // Inject instance name here? (compute instance names during lowering)
        stmt = LetStmt::make(var + ".loop_extent",
                             (max + 1) - min,
                             stmt);
        stmt = LetStmt::make(var + ".loop_min", min, stmt);
        stmt = LetStmt::make(var + ".loop_max", max, stmt);
    }

    // Define the loop mins and extents for the reduction domain (if there is any)
    // in terms of the mins and maxs produced by bounds inference
    for (const ReductionVariable &rv : s.rvars()) {
        string p = prefix + rv.var;
        Expr rmin = Variable::make(Int(32), p + ".min");
        Expr rmax = Variable::make(Int(32), p + ".max");
        stmt = LetStmt::make(p + ".loop_min", rmin, stmt);
        stmt = LetStmt::make(p + ".loop_max", rmax, stmt);
        stmt = LetStmt::make(p + ".loop_extent", rmax - rmin + 1, stmt);
    }

    return stmt;
}

// Build a loop nest about a provide node using a schedule
Stmt build_provide_loop_nest(string func_name,
                             string prefix,
                             int start_fuse,
                             const vector<string> &dims,
                             const Definition &def,
                             bool is_update) {

    internal_assert(!is_update == def.is_init());

    // Default stored values
    vector<Expr> site(def.args().size());
    vector<Expr> values(def.values().size());
    for (size_t i = 0; i < values.size(); i++) {
        Expr v = def.values()[i];
        v = qualify(prefix, v);
        values[i] = v;
        debug(3) << "Value " << i << " = " << v << "\n";
    }

    // Default stored locations
    for (size_t i = 0; i < def.args().size(); i++) {
        Expr s = def.args()[i];
        s = qualify(prefix, s);
        site[i] = s;
        debug(3) << "Site " << i << " = " << s << "\n";
    }

    // Default schedule/values if there is no specialization
    Stmt stmt = build_provide_loop_nest_helper(
        func_name, prefix, start_fuse, dims, site, values,
        def.split_predicate(), def.schedule(), is_update);

    // Make any specialized copies
    const vector<Specialization> &specializations = def.specializations();
    for (size_t i = specializations.size(); i > 0; i--) {
        Expr c = specializations[i-1].condition;
        const Definition &s_def = specializations[i-1].definition;

        Stmt then_case =
            build_provide_loop_nest(func_name, prefix, start_fuse, dims, s_def, is_update);

        stmt = IfThenElse::make(c, then_case, stmt);
    }

    return stmt;
}

// Turn a function into a loop nest that computes it. It will
// refer to external vars of the form function_name.arg_name.min
// and function_name.arg_name.extent to define the bounds over
// which it should be realized. It will compute at least those
// bounds (depending on splits, it may compute more). This loop
// won't do any allocation.
Stmt build_produce(Function f, const Target &target) {

    if (f.has_extern_definition()) {
        // Call the external function

        // Build an argument list
        vector<Expr> extern_call_args;
        const vector<ExternFuncArgument> &args = f.extern_arguments();

        const string &extern_name = f.extern_function_name();

        vector<pair<string, Expr>> lets;

        // Iterate through all of the input args to the extern
        // function building a suitable argument list for the
        // extern function call.
        vector<Expr> buffers_to_annotate;
        vector<Expr> buffers_contents_to_annotate;
        for (const ExternFuncArgument &arg : args) {
            if (arg.is_expr()) {
                extern_call_args.push_back(arg.expr);
            } else if (arg.is_func()) {
                Function input(arg.func);
                for (int k = 0; k < input.outputs(); k++) {
                    string buf_name = input.name();
                    if (input.outputs() > 1) {
                        buf_name += "." + std::to_string(k);
                    }
                    buf_name += ".buffer";
                    Expr buffer = Variable::make(type_of<struct buffer_t *>(), buf_name);
                    extern_call_args.push_back(buffer);
                    buffers_to_annotate.push_back(buffer);
                    buffers_contents_to_annotate.push_back(buffer);
                }
            } else if (arg.is_buffer()) {
                BufferPtr b = arg.buffer;
                Parameter p(b.type(), true, b.dimensions(), b.name());
                p.set_buffer(b);
                string buf_name = b.name() + ".buffer";
                Expr buf = Variable::make(type_of<struct buffer_t *>(), buf_name, p);
                extern_call_args.push_back(buf);
                buffers_to_annotate.push_back(buf);
                buffers_contents_to_annotate.push_back(buf);
            } else if (arg.is_image_param()) {
                Parameter p = arg.image_param;
                string buf_name = p.name() + ".buffer";
                Expr buf = Variable::make(type_of<struct buffer_t *>(), buf_name, p);
                extern_call_args.push_back(buf);
                // Do not annotate ImageParams: both the buffer_t itself,
                // and the contents it points to, should be filled by the caller;
                // if we mark it here, we might mask a missed initialization.
                // buffers_to_annotate.push_back(buf);
                // buffers_contents_to_annotate.push_back(buf);
            } else {
                internal_error << "Bad ExternFuncArgument type\n";
            }
        }

        // Grab the buffer_ts representing the output. If the store
        // level matches the compute level, then we can use the ones
        // already injected by allocation bounds inference. If it's
        // the output to the pipeline then it will similarly be in the
        // symbol table.
        if (f.schedule().store_level() == f.schedule().compute_level()) {
            for (int j = 0; j < f.outputs(); j++) {
                string buf_name = f.name();
                if (f.outputs() > 1) {
                    buf_name += "." + std::to_string(j);
                }
                buf_name += ".buffer";
                Expr buffer = Variable::make(type_of<struct buffer_t *>(), buf_name);
                extern_call_args.push_back(buffer);
                // Since this is a temporary, internal-only buffer, make sure it's marked.
                // (but not the contents! callee is expected to fill that in.)
                buffers_to_annotate.push_back(buffer);
            }
        } else {
            // Store level doesn't match compute level. Make an output
            // buffer just for this subregion.
            string stride_name = f.name();
            if (f.outputs() > 1) {
                stride_name += ".0";
            }
            string stage_name = f.name() + ".s0.";
            const vector<string> f_args = f.args();
            for (int j = 0; j < f.outputs(); j++) {

                vector<Expr> buffer_args(2);

                vector<Expr> top_left;
                for (int k = 0; k < f.dimensions(); k++) {
                    string var = stage_name + f_args[k];
                    top_left.push_back(Variable::make(Int(32), var + ".min"));
                }
                Expr host_ptr = Call::make(f, top_left, j);
                host_ptr = Call::make(Handle(), Call::address_of, {host_ptr}, Call::Intrinsic);

                buffer_args[0] = host_ptr;
                buffer_args[1] = make_zero(f.output_types()[j]);
                for (int k = 0; k < f.dimensions(); k++) {
                    string var = stage_name + f_args[k];
                    Expr min = Variable::make(Int(32), var + ".min");
                    Expr max = Variable::make(Int(32), var + ".max");
                    Expr stride = Variable::make(Int(32), stride_name + ".stride." + std::to_string(k));
                    buffer_args.push_back(min);
                    buffer_args.push_back(max - min + 1);
                    buffer_args.push_back(stride);
                }

                Expr output_buffer_t = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                                                  buffer_args, Call::Intrinsic);

                string buf_name = f.name() + "." + std::to_string(j) + ".tmp_buffer";
                extern_call_args.push_back(Variable::make(type_of<struct buffer_t *>(), buf_name));
                // Since this is a temporary, internal-only buffer, make sure it's marked.
                // (but not the contents! callee is expected to fill that in.)
                buffers_to_annotate.push_back(extern_call_args.back());
                lets.push_back(make_pair(buf_name, output_buffer_t));
            }
        }

        Stmt annotate;
        if (target.has_feature(Target::MSAN)) {
            // Mark the buffers as initialized before calling out.
            for (const auto &buffer: buffers_to_annotate) {
                // Return type is really 'void', but no way to represent that in our IR.
                // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
                Expr sizeof_buffer_t((uint64_t) sizeof(buffer_t));
                Stmt mark_buffer = Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized", {buffer, sizeof_buffer_t}, Call::Extern));
                if (annotate.defined()) {
                    annotate = Block::make(annotate, mark_buffer);
                } else {
                    annotate = mark_buffer;
                }
            }
            for (const auto &buffer: buffers_contents_to_annotate) {
                // Return type is really 'void', but no way to represent that in our IR.
                // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
                Stmt mark_contents = Evaluate::make(Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", {buffer}, Call::Extern));
                if (annotate.defined()) {
                    annotate = Block::make(annotate, mark_contents);
                } else {
                    annotate = mark_contents;
                }
            }
        }

        // Make the extern call
        Expr e = Call::make(Int(32), extern_name, extern_call_args,
                            f.extern_definition_is_c_plus_plus() ? Call::ExternCPlusPlus
                                                                 : Call::Extern);
        string result_name = unique_name('t');
        Expr result = Variable::make(Int(32), result_name);
        // Check if it succeeded
        Expr error = Call::make(Int(32), "halide_error_extern_stage_failed",
                                {extern_name, result}, Call::Extern);
        Stmt check = AssertStmt::make(EQ::make(result, 0), error);
        check = LetStmt::make(result_name, e, check);

        for (size_t i = 0; i < lets.size(); i++) {
            check = LetStmt::make(lets[i].first, lets[i].second, check);
        }

        if (annotate.defined()) {
            check = Block::make(annotate, check);
        }
        return check;
    } else {

        string prefix = f.name() + ".s0.";
        vector<string> dims = f.args();
        return build_provide_loop_nest(f.name(), prefix, -1, dims, f.definition(), false);
    }
}

// Build the loop nests that update a function (assuming it's a reduction).
vector<Stmt> build_update(Function f) {

    vector<Stmt> updates;

    for (size_t i = 0; i < f.updates().size(); i++) {
        const Definition &def = f.update(i);

        string prefix = f.name() + ".s" + std::to_string(i+1) + ".";

        vector<string> dims = f.args();
        Stmt loop = build_provide_loop_nest(f.name(), prefix, -1, dims, def, true);
        updates.push_back(loop);
    }

    return updates;
}

pair<Stmt, Stmt> build_production(Function func, const Target &target) {
    Stmt produce = build_produce(func, target);
    vector<Stmt> updates = build_update(func);

    // Combine the update steps
    Stmt merged_updates = Block::make(updates);
    return make_pair(produce, merged_updates);
}

// A schedule may include explicit bounds on some dimension. This
// injects assertions that check that those bounds are sufficiently
// large to cover the inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    const Schedule &s = func.schedule();
    for (size_t stage = 0; stage <= func.updates().size(); stage++) {
        for (size_t i = 0; i < s.bounds().size(); i++) {
            Bound b = s.bounds()[i];
            string prefix = func.name() + ".s" + std::to_string(stage) + "." + b.var;
            string min_name = prefix + ".min_unbounded";
            string max_name = prefix + ".max_unbounded";
            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);
            if (!b.min.defined()) {
                b.min = min_var;
            }
            if (!b.extent.defined()) {
                // This is just a bounds alignment, which always expands the region computed.
                continue;
            }

            Expr max_val = (b.extent + b.min) - 1;
            Expr min_val = b.min;

            Expr check = (min_val <= min_var) && (max_val >= max_var);
            Expr error_msg = Call::make(Int(32), "halide_error_explicit_bounds_too_small",
                                        {b.var, func.name(), min_val, max_val, min_var, max_var},
                                        Call::Extern);
            body = Block::make(AssertStmt::make(check, error_msg), body);
        }
    }

    return body;
}

class IsUsedInStmt : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->name == func) result = true;
    }

    // A reference to the function's buffers counts as a use
    void visit(const Variable *op) {
        if (op->type.is_handle() &&
            starts_with(op->name, func + ".") &&
            ends_with(op->name, ".buffer")) {
            result = true;
        }
    }

public:
    bool result;
    IsUsedInStmt(Function f) : func(f.name()), result(false) {
    }

};

bool function_is_used_in_stmt(Function f, Stmt s) {
    IsUsedInStmt is_called(f);
    s.accept(&is_called);
    return is_called.result;
}

class IsRealizedInStmt : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Realize *op) {
        //debug(0) << "FIND REALIZE " << op->name << "\n";
        IRVisitor::visit(op);
        if (op->name == func) result = true;
    }

public:
    bool result;
    IsRealizedInStmt(Function f) : func(f.name()), result(false) {}
};

bool function_is_already_realized_in_stmt(Function f, Stmt s) {
    IsRealizedInStmt is_realized(f);
    s.accept(&is_realized);
    return is_realized.result;
}

// Inject the allocation and realization of a function into an
// existing loop nest using its schedule
class InjectRealization : public IRMutator {
public:
    const Function &func;
    bool is_output, found_store_level, found_compute_level;
    const Target &target;

    InjectRealization(const Function &f, bool o, const Target &t) :
        func(f), is_output(o),
        found_store_level(false), found_compute_level(false),
        target(t) {}

private:

    Stmt build_pipeline(Stmt s) {
        pair<Stmt, Stmt> realization = build_production(func, target);

        Stmt producer;
        if (realization.first.defined() && realization.second.defined()) {
            producer = Block::make(realization.first, realization.second);
        } else if (realization.first.defined()) {
            producer = realization.first;
        } else {
            internal_assert(realization.second.defined());
            producer = realization.second;
        }
        producer = ProducerConsumer::make(func.name(), true, producer);
        Stmt consumer = ProducerConsumer::make(func.name(), false, s);

        return Block::make(producer, consumer);
    }

    Stmt build_realize(Stmt s) {
        if (!is_output) {
            Region bounds;
            string name = func.name();
            const vector<string> func_args = func.args();
            for (int i = 0; i < func.dimensions(); i++) {
                const string &arg = func_args[i];
                Expr min = Variable::make(Int(32), name + "." + arg + ".min_realized");
                Expr extent = Variable::make(Int(32), name + "." + arg + ".extent_realized");
                bounds.push_back(Range(min, extent));
            }

            s = Realize::make(name, func.output_types(), bounds, const_true(), s);
        }

        // This is also the point at which we inject explicit bounds
        // for this realization.
        if (target.has_feature(Target::NoAsserts)) {
            return s;
        } else {
            return inject_explicit_bounds(s, func);
        }
    }

    using IRMutator::visit;

    void visit(const For *for_loop) {
        debug(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
        const LoopLevel &compute_level = func.schedule().compute_level();
        const LoopLevel &store_level = func.schedule().store_level();

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr>> lets;
        while (const LetStmt *l = body.as<LetStmt>()) {
            lets.push_back(make_pair(l->name, l->value));
            body = l->body;
        }

        // Can't schedule extern things inside a vector for loop
        if (func.has_extern_definition() &&
            func.schedule().compute_level().is_inline() &&
            for_loop->for_type == ForType::Vectorized &&
            !function_is_already_realized_in_stmt(func, for_loop) &&
            function_is_used_in_stmt(func, for_loop)) {

            // If we're trying to inline an extern function, schedule it here and bail out
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return;
        }

        body = mutate(body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            if (!function_is_already_realized_in_stmt(func, body) &&
                (function_is_used_in_stmt(func, body) || is_output)) {
                //debug(0) << "Injecting realization of " << func.name() << " around node " << for_loop->name << "\n";
                body = build_pipeline(body);
            }
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            internal_assert(found_compute_level)
                << "The compute loop level was not found within the store loop level!\n";

            if (!function_is_already_realized_in_stmt(func, body) &&
                (function_is_used_in_stmt(func, body) || is_output)) {
                body = build_realize(body);
            }

            found_store_level = true;
        }

        // Reinstate the let statements
        for (size_t i = lets.size(); i > 0; i--) {
            body = LetStmt::make(lets[i - 1].first, lets[i - 1].second, body);
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->device_api,
                             body);
        }
    }

    // If we're an inline update or extern, we may need to inject a realization here
    virtual void visit(const Provide *op) {
        if (op->name != func.name() &&
            !func.is_pure() &&
            func.schedule().compute_level().is_inline() &&
            function_is_used_in_stmt(func, op)) {

            // Prefix all calls to func in op
            stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
        } else {
            stmt = op;
        }
    }
};

std::ostream& operator<<(std::ostream& out, const std::vector<Function>& v) {
    out << "{ ";
    for (size_t i = 0; i < v.size(); ++i) {
        out << v[i].name();
        if (i != v.size() - 1) {
            out << ", ";
        }
    }
    out << " }";
    return out;
}

class InjectStmt : public IRMutator {
public:
    Stmt injected_stmt;
    bool found_compute_level;
    LoopLevel compute_level;

    InjectStmt(const Stmt &s, const LoopLevel &compute_level)
        : injected_stmt(s), found_compute_level(false), compute_level(compute_level) {}

private:

    void visit(const For *for_loop) {
        Stmt body = mutate(for_loop->body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            body = Block::make(body, injected_stmt);
            found_compute_level = true;
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->device_api,
                             body);
        }
    }
};

Stmt inject_stmt(Stmt root, Stmt injected, const LoopLevel &level) {
    if (!root.defined()) {
        return injected;
    }
    if (!injected.defined()) {
        return root;
    }
    if (level.is_inline() || level.is_root()) {
        return Block::make(root, injected);
    }
    InjectStmt injector(injected, level);
    root = injector.mutate(root);
    internal_assert(injector.found_compute_level) << "Compute level: " << level.to_string() << "\n"
        << "root:\n" << root << "\ninjected:\n" << injected << "\n";
    return root;
}

class SubstituteBound : public IRMutator {
public:
    map<string, Expr> &bounds;
    const map<string, Expr> &replacements;
    SubstituteBound(map<string, Expr> &b, const map<string, Expr> &r) : bounds(b), replacements(r) {}

private:
    using IRMutator::visit;

    void visit(const LetStmt *op) {
        //debug(0) << "VISITING LET: " << op->name << "\n";
        auto iter = bounds.find(op->name);
        if (iter != bounds.end()) {
            iter->second = op->value;
            //debug(0) << "***Push " << iter->first << " -> " << bounds[op->name] << "\n";
        }
        IRMutator::visit(op);
    }

    void visit(const For *op) {
        //TODO(psuriana): replace the name with a new name and define the let statement wrapping the for loop
        //debug(0) << "VISIT FOR: " << op->name << "\n";

        const Variable *min_var = op->min.as<Variable>();
        const Variable *extent_var = op->extent.as<Variable>();
        if (min_var && extent_var) {
            Expr min_val, extent_val;
            {
                const auto it = replacements.find(min_var->name);
                if (it != replacements.end()) {
                    //debug(0) << "****REPLACEMENT MIN: " << min_var->name << " -> " << it->second << "\n";
                    min_val = it->second;
                }
            }
            {
                const auto it = replacements.find(extent_var->name);
                if (it != replacements.end()) {
                    //debug(0) << "****REPLACEMENT EXTENT: " << extent_var->name << " -> " << it->second << "\n";
                    extent_val = it->second;
                }
            }

            if (!min_val.defined()|| !extent_val.defined()) {
                IRMutator::visit(op);
                return;
            }

            Stmt body = mutate(op->body);

            size_t last_dot = op->name.rfind('.');
            internal_assert(last_dot != string::npos);
            string new_var = op->name.substr(0, last_dot) + ".fused." + op->name.substr(last_dot + 1);

            stmt = For::make(new_var, Variable::make(Int(32), new_var + ".loop_min"),
                             Variable::make(Int(32), new_var + ".loop_extent"),
                             op->for_type, op->device_api, body);

            stmt = LetStmt::make(new_var + ".loop_max", simplify(min_val + extent_val - 1), stmt);

            stmt = LetStmt::make(new_var + ".loop_min", min_val, stmt);
            stmt = LetStmt::make(new_var + ".loop_extent", extent_val, stmt);

            stmt = substitute(op->name, Variable::make(Int(32), new_var), stmt);

            //debug(0) << "\n******\nRESULT:\n" << stmt << "\n";
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt extract_bounds(Stmt s, map<string, Expr> &bounds, const map<string, Expr> &replacements) {
    if (!s.defined()) {
        return s;
    }
    SubstituteBound subs(bounds, replacements);
    s = subs.mutate(s);
    return s;
}

// Inject the allocation and realization of a group of functions which are
// to be fused into an existing loop nest using its schedule.
class InjectGroupRealization : public IRMutator {
public:
    vector<Function> &group; // Member of the fused loop starting from the first to be realized to the last
    const vector<bool> &is_output_list; // List of booleans indicating if group[i] is an output
    bool found_store_level, found_compute_level;
    const Target &target;
    LoopLevel compute_level;
    LoopLevel store_level;
    const map<string, Function> &env;

    InjectGroupRealization(vector<Function> &g, const vector<bool> &o, const Target &t,
                           const vector<string> &order, const map<string, Function> &env)
            : group(g), is_output_list(o), found_store_level(false), found_compute_level(false), target(t), env(env) {
        internal_assert(!group.empty());
        internal_assert(group.size() == is_output_list.size());

        compute_level = group[0].schedule().compute_level();
        store_level = group[0].schedule().store_level();
        internal_assert(!compute_level.is_inline());
    }

private:

    Stmt build_pipeline_group(Stmt s) {
        vector<bool> skip(group.size(), true);
        for (size_t i = 0; i < group.size(); ++i) {
            if (function_is_used_in_stmt(group[i], s) || is_output_list[i]) {
                skip[i] = false;
            }
        }

        // Add the consumer nodes
        Stmt consume = s;
        for (size_t i = group.size(); i > 0; --i) {
            consume = ProducerConsumer::make(group[i-1].name(), false, consume);
        }

        // Build the loops
        map<string, Expr> bounds; // The original bounds of the loopness
        map<string, Expr> replacements;

        Stmt produce;
        for (size_t i = 0; i < group.size(); ++i) {
            if (!skip[i]) {
                produce = build_produce(group[i], produce, bounds, replacements);
            }
        }

        // Now, replace the all the fused loops with the appropriate bounds
        /*debug(0) << "\n******\nReplacement:\n";
        for (const auto &iter : replacements) {
            debug(0) << iter.first << " -> " << iter.second << "\n";
        }
        debug(0) << "\n";

        debug(0) << "\n******\nBEFORE BOUNDS:\n";
        for (const auto &iter : bounds) {
            debug(0) << iter.first << " -> " << iter.second << "\n";
        }
        debug(0) << "\n";*/

        //debug(3) << "\n*********\nOLD LEAVES: \n" << produce << "\n";
        produce = extract_bounds(produce, bounds, replacements);
        //debug(3) << "\n*********\nNEW LEAVES: \n" << produce << "\n";

        /*debug(0) << "\n******\nBOUNDS:\n";
        for (const auto &iter : bounds) {
            debug(0) << iter.first << " -> " << iter.second << "\n";
        }
        debug(0) << "\n";*/

        //debug(0) << "\n***START REPLACING\n";

        /*for (size_t i = 0; i < group.size(); ++i) {
            if (!skip[i]) {
                //TODO(psuriana): should't use the bound of skipped function
                produce = replace_with_union_bound(group[i], produce, bounds);
            }
        }*/
        produce = replace_with_union_bound(group[0], produce, bounds);

        // Add the producer nodes
        for (size_t i = group.size(); i > 0; --i) {
            produce = ProducerConsumer::make(group[i-1].name(), true, produce);
        }

        return Block::make(produce, consume);

        //TODO(psuriana): take care of parallel/vectorize (necessary???)
    }

    /*Stmt build_produce(Function f, Stmt produce) {
        string prefix = f.name() + ".s0.";
        produce = build_produce_definition(f, prefix, f.definition(), false, produce);

        for (size_t j = 0; j < f.updates().size(); ++j) {
            const Definition &def = f.updates()[j];
            string prefix = f.name() + ".s" + std::to_string(j+1) + ".";
            produce = build_produce_definition(f, prefix, def, true, produce);
        }
        return produce;
    }*/

    Stmt build_produce(Function f, Stmt produce, map<string, Expr> &bounds, map<string, Expr> &replacements) {
        string prefix = f.name() + ".s0.";
        produce = inject_stmt(produce, build_produce_definition(f, prefix, f.definition(), false, bounds, replacements),
                              f.definition().schedule().fuse_level());

        for (size_t j = 0; j < f.updates().size(); ++j) {
            const Definition &def = f.updates()[j];
            string prefix = f.name() + ".s" + std::to_string(j+1) + ".";
            produce = inject_stmt(produce, build_produce_definition(f, prefix, def, true, bounds, replacements),
                                  def.schedule().fuse_level());
        }
        return produce;
    }

    Stmt replace_with_union_bound(Function f, Stmt produce, map<string, Expr> &bounds) {
        string prefix = f.name() + ".s0";
        debug(3) << "\n*********\nBEFORE REPLACE WITH UNION " << prefix << ": \n" << produce << "\n";
        produce = replace_with_union_bound_definition(f, prefix, f.definition(), produce, bounds);
        debug(3) << "\n*********\nAFTER REPLACE WITH UNION " << prefix << ": \n" << produce << "\n";

        /*for (size_t j = 0; j < f.updates().size(); ++j) {
            const Definition &def = f.updates()[j];
            string prefix = f.name() + ".s" + std::to_string(j+1);
            debug(0) << "\n*********\nBEFORE REPLACE WITH UNION " << prefix << ": \n" << produce << "\n";
            produce = replace_with_union_bound_definition(f, prefix, def, produce, bounds);
            debug(0) << "\n*********\nAFTER REPLACE WITH UNION " << prefix << ": \n" << produce << "\n";
        }*/
        return produce;
    }

    void collect_all_dependence_helper(const string &prefix, const Definition &def, const FusedPair &p,
                                       vector<FusedPair> &dependence, set<string> &visited) {
        visited.insert(prefix);
        dependence.push_back(p);
        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            string prefix_2 = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + pair.var_name;
            if (visited.find(prefix_2) == visited.end()) {
                const Function &f = env.find(pair.func_2)->second;
                const Definition &def_2 = (pair.stage_2 == 0) ? f.definition() : f.update(pair.stage_2 - 1);
                collect_all_dependence_helper(prefix_2, def_2, pair, dependence, visited);
            }
        }
    }

    vector<FusedPair> collect_all_dependence(const Definition &def) {
        set<string> visited;
        vector<FusedPair> dependence;

        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            string prefix = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + pair.var_name;
            if (visited.find(prefix) == visited.end()) {
                const Function &f = env.find(pair.func_2)->second;
                const Definition &def_2 = (pair.stage_2 == 0) ? f.definition() : f.update(pair.stage_2 - 1);
                collect_all_dependence_helper(prefix, def_2, pair, dependence, visited);
            }
        }
        return dependence;
    }

    Stmt replace_with_union_bound_definition(Function f, const string &prefix, const Definition &def,
                                             Stmt produce, map<string, Expr> &bounds) {
        // Compute the loop bounds for each dimension.
        vector<Dim> dims = def.schedule().dims(); // From inner to outer

        map<string, Expr> replacements;

        vector<FusedPair> dependence = collect_all_dependence(def);
        /*debug(0) << "\n*******\nDEPENDENCE OF " << prefix << ":\n";
        for (const auto &pair : dependence) {
            string prefix = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + pair.var_name;
            debug(0) << "   " << prefix << "\n";
        }
        debug(0) << "\n";*/

        for (const FusedPair &pair : dependence) {
            const auto iter = std::find_if(dims.begin(), dims.end(),
                [&pair](const Dim& d) { return var_name_match(d.var, pair.var_name); });
            internal_assert(iter != dims.end());
            // Should ignore the __outermost dummy dimension.
            for (size_t i = iter - dims.begin(); i < dims.size() - 1; ++i) {
                string var_2 = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + dims[i].var;
                internal_assert(bounds.count(var_2 + ".loop_min"));
                internal_assert(bounds.count(var_2 + ".loop_max"));
                internal_assert(bounds.count(var_2 + ".loop_extent"));
                Expr min_2 = bounds[var_2 + ".loop_min"];
                Expr max_2 = bounds[var_2 + ".loop_max"];
                Expr extent_2 = bounds[var_2 + ".loop_extent"];

                //debug(0) << "++++DEPENDENCE: " << var_2 << ", min: " << min_2 << ", max: " << max_2 << ", extent: " << extent_2 << "\n";

                string var_1 = prefix + "." + dims[i].var;
                internal_assert(bounds.count(var_1 + ".loop_min")) << var_1 << "\n";
                internal_assert(bounds.count(var_1 + ".loop_max"));
                internal_assert(bounds.count(var_1 + ".loop_extent"));

                Expr min_1, max_1, extent_1;
                const auto it = replacements.find(var_1 + ".loop_min");
                if (it == replacements.end()) {
                    min_1 = bounds[var_1 + ".loop_min"];
                    max_1 = bounds[var_1 + ".loop_max"];
                    extent_1 = bounds[var_1 + ".loop_extent"];
                } else {
                    min_1 = replacements[var_1 + ".loop_min"];
                    max_1 = replacements[var_1 + ".loop_max"];
                    extent_1 = replacements[var_1 + ".loop_extent"];
                }

                replacements[var_1 + ".loop_min"] = simplify(min(min_1, min_2));
                replacements[var_1 + ".loop_max"] = simplify(max(max_1, max_2));
                replacements[var_1 + ".loop_extent"] =
                    simplify((replacements[var_1 + ".loop_max"] + 1) - replacements[var_1 + ".loop_min"]);
            }
        }

        // The loop bounds should be the union of the original bounds with the
        // bounds of whatever functions are fused with it.
        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            const auto iter = std::find_if(dims.begin(), dims.end(),
                [&pair](const Dim& d) { return var_name_match(d.var, pair.var_name); });
            internal_assert(iter != dims.end());
            // Should ignore the __outermost dummy dimension.
            for (size_t i = iter - dims.begin(); i < dims.size() - 1; ++i) {
                string var_2 = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + dims[i].var;
                internal_assert(bounds.count(var_2 + ".loop_min"));
                internal_assert(bounds.count(var_2 + ".loop_max"));
                internal_assert(bounds.count(var_2 + ".loop_extent"));

                // Reset the extents
                string var_1 = pair.func_1 + ".s" + std::to_string(pair.stage_1) + ".fused." + dims[i].var;
                Expr val = Variable::make(Int(32), var_1);
                bounds[var_2 + ".loop_min"] = val;
                bounds[var_2 + ".loop_max"] = val;
                bounds[var_2 + ".loop_extent"] = make_const(Int(32), 1);
            }
        }

        // Now, replace the all the fused loops with the appropriate bounds
        /*debug(0) << "\n******\nReplacement " << prefix << ":\n";
        for (const auto &iter : replacements) {
            debug(0) << iter.first << " -> " << iter.second << "\n";
        }
        debug(0) << "\n";*/

        //debug(0) << "\n*********\nBEFORE REPLACEMENT: \n" << produce << "\n";
        map<string, Expr> empty_bounds;
        produce = extract_bounds(produce, empty_bounds, replacements);
        //debug(0) << "\n*********\nAFTER REPLACEMENT: \n" << produce << "\n";

        return produce;
    }

    Stmt build_produce_definition(Function f, const string &prefix, const Definition &def,
                                  bool is_update, map<string, Expr> &bounds, map<string, Expr> &replacements) {
        // Compute the loop bounds for each dimension.
        vector<Dim> dims = def.schedule().dims(); // From inner to outer

        const LoopLevel &fuse_level = def.schedule().fuse_level();
        size_t start_fuse = dims.size();
        if (!fuse_level.is_inline() && !fuse_level.is_root()) {
            const auto iter = std::find_if(dims.begin(), dims.end(),
                [&fuse_level](const Dim& d) { return var_name_match(d.var, fuse_level.var().name()); });
            internal_assert(iter != dims.end());
            start_fuse = iter - dims.begin();
        }

        // From start_fuse to outermost, the dimensions are fused with a 'parent' function.
        // Ignore the __outermost dimensions.
        for (size_t i = start_fuse; i < dims.size() - 1; ++i) {
            string var = fuse_level.func().name() + ".s" + std::to_string(fuse_level.stage()) + "." + dims[i].var;
            Expr val = Variable::make(Int(32), var);
        }

        // The loop bounds should be the union of the original bounds with the
        // bounds of whatever functions are fused with it.
        vector<pair<string, Expr>> add_lets;
        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            const auto iter = std::find_if(dims.begin(), dims.end(),
                [&pair](const Dim& d) { return var_name_match(d.var, pair.var_name); });
            internal_assert(iter != dims.end());
            start_fuse = std::min(start_fuse, (size_t)(iter - dims.begin()));
            // Should ignore the __outermost dummy dimension.
            for (size_t i = iter - dims.begin(); i < dims.size() - 1; ++i) {
                string var = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + dims[i].var;
                bounds.emplace(var + ".loop_min", Expr());
                bounds.emplace(var + ".loop_max", Expr());
                bounds.emplace(var + ".loop_extent", Expr());

                string var_orig = pair.func_1 + ".s" + std::to_string(pair.stage_1) + "." + dims[i].var;
                Expr val = Variable::make(Int(32), var_orig);
                replacements.emplace(var + ".loop_min", val);
                replacements.emplace(var + ".loop_max", val);
                replacements.emplace(var + ".loop_extent", make_const(Int(32), 1));

                bounds.emplace(var_orig + ".loop_min", Expr());
                bounds.emplace(var_orig + ".loop_max", Expr());
                bounds.emplace(var_orig + ".loop_extent", Expr());
            }
            // Add any "pure" dimensions that are not part of the fused, since it may
            // be refer later by the union in case of split. Ignore __outermost
            /*debug(0) << "\n*****\nARGS:\n";
            for (const auto &s : f.args()) {
                debug(0) << s << ", ";
            }
            debug(0) << "\n";

            debug(0) << "\n*****\nDIMS:\n";
            for (const auto &s : dims) {
                debug(0) << s.var << ", ";
            }
            debug(0) << "\n";*/

            for (size_t i = 0; i < f.args().size(); ++i) {
                const string &var_name = f.args()[i];
                const auto iter = std::find_if(dims.begin(), dims.end(),
                    [&var_name](const Dim& d) { return var_name_match(d.var, var_name); });
                if ((iter == dims.end()) || ((size_t)(iter - dims.begin()) < start_fuse)) {
                    string var = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + var_name;
                    //debug(0) << "ADDING LET OF " << var << "\n";

                    Expr max = Variable::make(Int(32), var + ".max");
                    Expr min = Variable::make(Int(32), var + ".min"); // Inject instance name here? (compute instance names during lowering)
                    add_lets.push_back(std::make_pair(var + ".loop_extent", (max + 1) - min));
                    add_lets.push_back(std::make_pair(var + ".loop_min", min));
                    add_lets.push_back(std::make_pair(var + ".loop_max", max));
                }
            }
        }

        // Now, replace the all the fused loops with the appropriate bounds
        /*debug(0) << "\n******\nLETS " << prefix << ":\n";
        for (const auto &iter : add_lets) {
            debug(0) << iter.first << " -> " << iter.second << "\n";
        }
        debug(0) << "\n";*/

        Stmt produce = build_provide_loop_nest(f.name(), prefix, start_fuse, f.args(), def, is_update);

        for (const auto &b : add_lets) {
            produce = LetStmt::make(b.first, b.second, produce);
        }

        //TODO(psuriana): need to select the schedule so we don't doubly schedule things
        // also need to put the subsequent definition at the right loop level
        return produce;
    }

    Stmt build_realize_group(Stmt s) {
        for (size_t i = group.size(); i > 0; --i) {
            if (function_is_used_in_stmt(group[i-1], s) || is_output_list[i-1]) {
                s = build_realize(s, group[i-1], is_output_list[i-1]);
            }
        }
        return s;
    }

    Stmt build_realize(Stmt s, Function func, bool is_output) {
        if (!is_output) {
            Region bounds;
            string name = func.name();
            const vector<string> func_args = func.args();
            for (int i = 0; i < func.dimensions(); i++) {
                const string &arg = func_args[i];
                Expr min = Variable::make(Int(32), name + "." + arg + ".min_realized");
                Expr extent = Variable::make(Int(32), name + "." + arg + ".extent_realized");
                bounds.push_back(Range(min, extent));
            }

            s = Realize::make(name, func.output_types(), bounds, const_true(), s);
        }

        // This is also the point at which we inject explicit bounds
        // for this realization.
        if (target.has_feature(Target::NoAsserts)) {
            return s;
        } else {
            return inject_explicit_bounds(s, func);
        }
    }

    using IRMutator::visit;

    void visit(const For *for_loop) {
        debug(3) << "InjectGroupRealization of " << group << " entering for loop over " << for_loop->name << "\n";

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr>> lets;
        while (const LetStmt *l = body.as<LetStmt>()) {
            lets.push_back(make_pair(l->name, l->value));
            body = l->body;
        }

        body = mutate(body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            body = build_pipeline_group(body);
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            internal_assert(found_compute_level)
                << "The compute loop level was not found within the store loop level!\n";
            body = build_realize_group(body);
            found_store_level = true;
        }

        // Reinstate the let statements
        for (size_t i = lets.size(); i > 0; i--) {
            body = LetStmt::make(lets[i - 1].first, lets[i - 1].second, body);
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->device_api,
                             body);
        }
    }
};


class ComputeLegalSchedules : public IRVisitor {
public:
    struct Site {
        bool is_parallel;
        LoopLevel loop_level;
    };
    vector<Site> sites_allowed;

    ComputeLegalSchedules(Function f, const map<string, Function> &env) : func(f), found(false), env(env) {}

private:
    using IRVisitor::visit;

    vector<Site> sites;
    Function func;
    bool found;
    const map<string, Function> &env;

    void visit(const For *f) {
        f->min.accept(this);
        f->extent.accept(this);
        size_t first_dot = f->name.find('.');
        size_t last_dot = f->name.rfind('.');
        internal_assert(first_dot != string::npos && last_dot != string::npos);
        string func = f->name.substr(0, first_dot);
        string var = f->name.substr(last_dot + 1);
        LoopLevel loop_level;
        if (func.empty()) {
            internal_assert(!var.empty());
            loop_level = LoopLevel::root();
        } else {
            auto it = env.find(func);
            internal_assert(it != env.end()) << "Unable to find Function " << func << " in env (Var = " << var << ")\n";
            loop_level = LoopLevel(it->second, Var(var));
        }
        Site s = {f->for_type == ForType::Parallel ||
                  f->for_type == ForType::Vectorized,
                  loop_level};
        sites.push_back(s);
        f->body.accept(this);
        sites.pop_back();
    }

    void register_use() {
        if (!found) {
            found = true;
            sites_allowed = sites;
        } else {
            vector<Site> common_sites;

            // Take the common sites between sites and sites_allowed
            for (const Site &s1 : sites) {
                for (const Site &s2 : sites_allowed) {
                    if (s1.loop_level.match(s2.loop_level)) {
                        common_sites.push_back(s1);
                        break;
                    }
                }
            }

            sites_allowed.swap(common_sites);
        }
    }

    void visit(const Call *c) {
        IRVisitor::visit(c);

        if (c->name == func.name()) {
            register_use();
        }
    }

    void visit(const Variable *v) {
        if (v->type.is_handle() &&
            starts_with(v->name, func.name() + ".") &&
            ends_with(v->name, ".buffer")) {
            register_use();
        }
    }
};

string schedule_to_source(Function f,
                          LoopLevel store_at,
                          LoopLevel compute_at) {
    std::ostringstream ss;
    ss << f.name();
    if (compute_at.is_inline()) {
        ss << ".compute_inline()";
    } else {
        if (!store_at.match(compute_at)) {
            if (store_at.is_root()) {
                ss << ".store_root()";
            } else {
                string store_var_name = store_at.var().name();
                if (store_var_name == Var::outermost().name()) {
                    store_var_name = "Var::outermost()";
                }
                ss << ".store_at(" << store_at.func().name() << ", " << store_var_name << ")";
            }
        }
        if (compute_at.is_root()) {
            ss << ".compute_root()";
        } else {
            string compute_var_name = compute_at.var().name();
            if (compute_var_name == Var::outermost().name()) {
                compute_var_name = "Var::outermost()";
            }
            ss << ".compute_at(" << compute_at.func().name() << ", " << compute_var_name << ")";
        }
    }
    ss << ";";
    return ss.str();
}

class StmtUsesFunc : public IRVisitor {
    using IRVisitor::visit;
    string func;
    void visit(const Call *op) {
        if (op->name == func) {
            result = true;
        }
        IRVisitor::visit(op);
    }
public:
    bool result = false;
    StmtUsesFunc(string f) : func(f) {}
};

class PrintUsesOfFunc : public IRVisitor {
    using IRVisitor::visit;

    int indent = 1;
    string func, caller;
    bool last_print_was_ellipsis = false;
    std::ostream &stream;

    void do_indent() {
        for (int i = 0; i < indent; i++) {
            stream << "  ";
        }
    }

    void visit(const For *op) {
        if (ends_with(op->name, Var::outermost().name()) ||
            ends_with(op->name, LoopLevel::root().to_string())) {
            IRVisitor::visit(op);
        } else {

            int old_indent = indent;

            StmtUsesFunc uses(func);
            op->body.accept(&uses);
            if (!uses.result) {
                if (!last_print_was_ellipsis) {
                    do_indent();
                    stream << "...\n";
                    last_print_was_ellipsis = true;
                }
            } else {
                do_indent();
                stream << "for " << op->name << ":\n";
                last_print_was_ellipsis = false;
                indent++;
            }

            IRVisitor::visit(op);
            indent = old_indent;
        }
    }

    void visit(const ProducerConsumer *op) {
        if (op->is_producer) {
            string old_caller = caller;
            caller = op->name;
            op->body.accept(this);
            caller = old_caller;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Call *op) {
        if (op->name == func) {
            do_indent();
            stream << caller << " uses " << func << "\n";
            last_print_was_ellipsis = false;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    PrintUsesOfFunc(string f, std::ostream &s) : func(f), stream(s) {}
};

void validate_schedule(Function f, Stmt s, const Target &target, bool is_output, const map<string, Function> &env) {

    // If f is extern, check that none of its inputs are scheduled inline.
    if (f.has_extern_definition()) {
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                Function g(arg.func);
                if (g.schedule().compute_level().is_inline()) {
                    user_error
                        << "Func " << g.name() << " cannot be scheduled to be computed inline, "
                        << "because it is used in the externally-computed function " << f.name() << "\n";
                }
            }
        }
    }

    // Emit a warning if only some of the steps have been scheduled.
    bool any_scheduled = f.schedule().touched();
    for (const Definition &r : f.updates()) {
        any_scheduled = any_scheduled || r.schedule().touched();
    }
    if (any_scheduled) {
        for (size_t i = 0; i < f.updates().size(); i++) {
            const Definition &r = f.update(i);
            if (!r.schedule().touched()) {
                user_warning << "Warning: Update step " << i
                             << " of function " << f.name()
                             << " has not been scheduled, even though some other"
                             << " steps have been. You may have forgotten to"
                             << " schedule it. If this was intentional, call "
                             << f.name() << ".update(" << i << ") to suppress"
                             << " this warning.\n";
            }
        }
    }

    // If the func is scheduled on the gpu, check that the relevant
    // api is enabled in the target.
    vector<Definition> definitions;
    definitions.push_back(f.definition());
    for (const Definition &def : f.updates()) {
        definitions.push_back(def);
    }

    for (size_t i = 0; i < definitions.size(); i++) {
        for (const Specialization &s : definitions[i].specializations()) {
            definitions.push_back(s.definition);
        }
    }

    for (const Definition &def : definitions) {
        const Schedule &s = def.schedule();
        for (const Dim &d : s.dims()) {
            if (!target.supports_device_api(d.device_api)) {
                user_error << "Schedule for Func " << f.name()
                           << " requires " << d.device_api
                           << " but no compatible target feature is enabled in target "
                           << target.to_string() << "\n";
            }
        }
    }

    LoopLevel store_at = f.schedule().store_level();
    LoopLevel compute_at = f.schedule().compute_level();

    // Outputs must be compute_root and store_root. They're really
    // store_in_user_code, but store_root is close enough.
    if (is_output) {
        if (store_at.is_root() && compute_at.is_root()) {
            return;
        } else {
            user_error << "Func " << f.name() << " is the output, so must"
                       << " be scheduled compute_root (which is the default).\n";
        }
    }

    // Inlining is allowed only if there is no specialization.
    if (store_at.is_inline() && compute_at.is_inline()) {
        user_assert(f.definition().specializations().empty())
            << "Func " << f.name() << " is scheduled inline, so it"
            << " must not have any specializations. Specialize on the"
            << " scheduled Func instead.\n";
        return;
    }

    // Otherwise inspect the uses to see what's ok.
    ComputeLegalSchedules legal(f, env);
    s.accept(&legal);

    bool store_at_ok = false, compute_at_ok = false;
    const vector<ComputeLegalSchedules::Site> &sites = legal.sites_allowed;
    size_t store_idx = 0, compute_idx = 0;
    for (size_t i = 0; i < sites.size(); i++) {
        if (sites[i].loop_level.match(store_at)) {
            store_at_ok = true;
            store_idx = i;
        }
        if (sites[i].loop_level.match(compute_at)) {
            compute_at_ok = store_at_ok;
            compute_idx = i;
        }
    }

    // Check there isn't a parallel loop between the compute_at and the store_at
    std::ostringstream err;

    if (store_at_ok && compute_at_ok) {
        for (size_t i = store_idx + 1; i <= compute_idx; i++) {
            if (sites[i].is_parallel) {
                err << "Func \"" << f.name()
                    << "\" is stored outside the parallel loop over "
                    << sites[i].loop_level.to_string()
                    << " but computed within it. This is a potential race condition.\n";
                store_at_ok = compute_at_ok = false;
            }
        }
    }

    if (!store_at_ok || !compute_at_ok) {
        err << "Func \"" << f.name() << "\" is computed at the following invalid location:\n"
            << "  " << schedule_to_source(f, store_at, compute_at) << "\n"
            << "Legal locations for this function are:\n";
        for (size_t i = 0; i < sites.size(); i++) {
            err << "  " << schedule_to_source(f, sites[i].loop_level, sites[i].loop_level) << "\n";
        }
        err << "\"" << f.name() << "\" is used in the following places:\n";
        PrintUsesOfFunc printer(f.name(), err);
        s.accept(&printer);

        user_error << err.str();
    }
}

void validate_fused_group_schedule_helper(const string &fn, size_t stage,
                                          const Definition &def_1,
                                          const map<string, Function> &env) {
    for (const auto &p : def_1.schedule().fused_pairs()) {
        internal_assert((fn == p.func_1) && (stage == p.stage_1));

        const Function &func_1 = env.find(p.func_1)->second;
        const Function &func_2 = env.find(p.func_2)->second;
        const Definition &def_2 = (p.stage_2 == 0) ? func_2.definition() : func_2.update(p.stage_2 - 1);

        // f2.compute_with(f1, var) is allowed only if f2 has no specializations.
        user_assert(func_2.definition().specializations().empty())
            << "Func " << func_2.name() << " is scheduled to be computed with "
            << func_1.name() << ", so it must not have any specializations.\n";

        // Verify that the functions being computed with are not scheduled inline.
        user_assert(!func_1.definition().schedule().compute_level().is_inline())
            << "Invalid compute_with: " << p.func_1 << ".s" << p.stage_1
            << " is scheduled inline.\n";
        user_assert(!func_2.definition().schedule().compute_level().is_inline())
            << "Invalid compute_with: " << p.func_2 << ".s" << p.stage_2
            << " is scheduled inline.\n";

        // Verify that the functions being computed with does not have extern definitions.
        user_assert(!func_1.has_extern_definition())
            << "Invalid compute_with: " << p.func_1 << ".s" << p.stage_1
            << " has extern definition.\n";
        user_assert(!func_2.has_extern_definition())
            << "Invalid compute_with: " << p.func_2 << ".s" << p.stage_2
            << " has extern definition.\n";

        // Verify that their dimensions up to "var_name" are the same.
        const vector<Dim> &dims_1 = def_1.schedule().dims();
        const vector<Dim> &dims_2 = def_2.schedule().dims();

        // Assert that the variable specified in compute_with is in the dim list.
        const auto iter_1 = std::find_if(dims_1.begin(), dims_1.end(),
            [&p](const Dim& d) { return var_name_match(d.var, p.var_name); });
        user_assert(iter_1 != dims_1.end())
            << "Invalid compute_with: cannot find " << p.var_name << " in "
            << p.func_1 << ".s" << p.stage_1 << "\n";

        const auto iter_2 = std::find_if(dims_2.begin(), dims_2.end(),
            [&p](const Dim& d) { return var_name_match(d.var, p.var_name); });
        user_assert(iter_2 != dims_2.end())
            << "Invalid compute_with: cannot find " << p.var_name << " in "
            << p.func_2 << ".s" << p.stage_2 << "\n";

        size_t start_fuse_1 = iter_1 - dims_1.begin();
        size_t start_fuse_2 = iter_2 - dims_2.begin();

        int n_fused = dims_1.size() - start_fuse_1;
        user_assert(n_fused == (int)(dims_2.size() - start_fuse_2))
            << "Invalid compute_with: # of fused dims of " << p.func_1 << ".s"
            << p.stage_1 << " and " << p.func_2 << ".s" << p.stage_2 << " do not match.\n";

        for (int i = 0; i < n_fused; ++i) {
            if (dims_1[start_fuse_1 + i] != dims_2[start_fuse_2 + i]) {
                user_error << "Invalid compute_with: dims " << i << " of " << p.func_1 << ".s"
                           << p.stage_1 << "(" << dims_1[start_fuse_1 + i].var << ") and " << p.func_2
                           << ".s" << p.stage_2 << "(" << dims_2[start_fuse_2 + i].var << ") do not match.\n";
            }
        }

        // Verify that they are computed at the same loop level.
        user_assert((p.func_1 == p.func_2) ||
                    (func_1.definition().schedule().compute_level() ==
                     func_2.definition().schedule().compute_level()))
            << "Invalid compute_with: the compute levels of " << p.func_1 << ".s" << p.stage_1
            << " (computed at " << func_1.definition().schedule().compute_level().to_string()
            << ") and " << p.func_2 << ".s" << p.stage_2 << " ("
            << func_2.definition().schedule().compute_level().to_string() << ") do not match.\n";
    }
}

void validate_fused_groups_schedule(const vector<vector<string>> &fused_groups,
                                   const map<string, Function> &env) {
    // Assert that the dimensions and compute level of functions within a fuse
    // group from outermost loop to the innermost fused loop match exactly.
    for (const vector<string> &group : fused_groups) {
        for (const auto &fn : group) {
            const auto iter = env.find(fn);
            internal_assert(iter != env.end());

            validate_fused_group_schedule_helper(
                iter->first, 0, iter->second.definition(), env);
            for (size_t i = 0; i < iter->second.updates().size(); ++i) {
                validate_fused_group_schedule_helper(
                    iter->first, i + 1, iter->second.updates()[i], env);
            }
        }
    }
}

class RemoveLoopsOverOutermost : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        if (ends_with(op->name, ".__outermost") &&
            is_one(simplify(op->extent)) &&
            op->device_api == DeviceAPI::None) {
            stmt = mutate(substitute(op->name, op->min, op->body));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (ends_with(op->name, ".__outermost.loop_extent") ||
            ends_with(op->name, ".__outermost.loop_min") ||
            ends_with(op->name, ".__outermost.loop_max")) {
            stmt = mutate(substitute(op->name, simplify(op->value), op->body));
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt schedule_functions(const vector<Function> &outputs,
                        const vector<string> &order,
                        const vector<vector<string>> &fused_groups,
                        const map<string, Function> &env,
                        const Target &target,
                        bool &any_memoized) {

    // Assert that the fused group is sorted based on the realization order
    auto iter = order.begin();
    for (const vector<string> &group : fused_groups) {
        internal_assert(!group.empty());
        for (const string &fn : group) {
            internal_assert(iter != order.end());
            internal_assert(*iter == fn);
            iter++;
        }
    }
    internal_assert(iter == order.end());

    string root_var = LoopLevel::root().to_string();
    Stmt s = For::make(root_var, 0, 1, ForType::Serial, DeviceAPI::Host, Evaluate::make(0));

    any_memoized = false;

    validate_fused_groups_schedule(fused_groups, env);

    for (size_t i = fused_groups.size(); i > 0; --i) {
        const vector<string> &group = fused_groups[i-1];
        vector<Function> funcs(group.size());
        vector<bool> is_output_list(group.size(), false);
        for (size_t j = group.size(); j > 0; --j) {
            funcs[j-1] = env.find(group[j-1])->second;

            for (Function o : outputs) {
                is_output_list[j-1] = is_output_list[j-1] | o.same_as(funcs[j-1]);
            }
            validate_schedule(funcs[j-1], s, target, is_output_list[j-1], env);
            any_memoized = any_memoized || funcs[j-1].schedule().memoized();
        }

        if (group.size() == 1) {
            // 1 member only -> no loop fusion
            if (funcs[0].can_be_inlined() &&
                funcs[0].schedule().compute_level().is_inline()) {
                debug(1) << "Inlining " << funcs[0].name() << '\n';
                s = inline_function(s, funcs[0]);
            } else {
                debug(1) << "Injecting realization of " << funcs[0].name() << '\n';
                InjectRealization injector(funcs[0], is_output_list[0], target);
                s = injector.mutate(s);
                internal_assert(injector.found_store_level && injector.found_compute_level);
            }
        } else {
            InjectGroupRealization injector(funcs, is_output_list, target, order, env);
            s = injector.mutate(s);
            internal_assert(injector.found_store_level && injector.found_compute_level);
        }

        debug(3) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    internal_assert(root_loop);
    s = root_loop->body;

    // We can also remove all the loops over __outermost now.
    s = RemoveLoopsOverOutermost().mutate(s);

    return s;

}

}
}
