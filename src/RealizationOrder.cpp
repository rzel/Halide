#include <set>

#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Func.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::set;
using std::vector;
using std::pair;

void realization_order_dfs(string current,
                           const map<string, set<string>> &graph,
                           set<string> &visited,
                           set<string> &result_set,
                           vector<string> &order) {
    visited.insert(current);

    map<string, set<string>>::const_iterator iter = graph.find(current);
    internal_assert(iter != graph.end());

    for (const string &fn : iter->second) {
        if (visited.find(fn) == visited.end()) {
            realization_order_dfs(fn, graph, visited, result_set, order);
        } else if (fn != current) { // Self-loops are allowed in update stages
            internal_assert(result_set.find(fn) != result_set.end())
                << "Stuck in a loop computing a realization order. "
                << "Perhaps this pipeline has a loop?\n";
        }
    }

    result_set.insert(current);
    order.push_back(current);
}

void find_fuse_group_dfs(string current,
                         const map<string, set<string>> &fuse_adjacency_list,
                         set<string> &visited,
                         vector<string> &group) {
    visited.insert(current);
    group.push_back(current);

    map<string, set<string>>::const_iterator iter = fuse_adjacency_list.find(current);
    internal_assert(iter != fuse_adjacency_list.end()) << "current: " << current << "\n";

    for (const string &fn : iter->second) {
        if (visited.find(fn) == visited.end()) {
            find_fuse_group_dfs(fn, fuse_adjacency_list, visited, group);
        }
    }
}

vector<vector<string>> find_fuse_group(const map<string, Function> &env,
                                       const map<string, set<string>> &fuse_adjacency_list) {
    set<string> visited;
    vector<vector<string>> result;

    for (const auto &iter : env) {
        if (visited.find(iter.first) == visited.end()) {
            vector<string> group;
            find_fuse_group_dfs(iter.first, fuse_adjacency_list, visited, group);
            result.push_back(std::move(group));
        }
    }
    return result;
}

void collect_fused_pairs(const map<string, Function> &env,
                         const map<string, map<string, Function>> &indirect_calls,
                         const vector<FusedPair> &pairs,
                         vector<FusedPair> &func_fused_pairs,
                         map<string, set<string>> &graph,
                         map<string, set<string>> &fuse_adjacency_list) {
    for (const auto &p : pairs) {
        if (env.find(p.func_2) == env.end()) {
            // Since func_2 is not being called by anyone, might as well skip this fused pair.
            continue;
        }

        // Assert no duplicates (this technically should not have been possible from the front-end)
        internal_assert(std::find(func_fused_pairs.begin(), func_fused_pairs.end(), p) == func_fused_pairs.end())
             << "Found duplicates of fused pair (" << p.func_1 << ".s" << p.stage_1 << ", "
             << p.func_2 << ".s" << p.stage_2 << ", " << p.var_name << ")\n";

        // Assert no dependencies among the functions that are computed_with.
        // Self-dependecy is allowed in update stages.
        if (p.func_1 != p.func_2) {
            const auto &callees_1 = indirect_calls.find(p.func_1);
            if (callees_1 != indirect_calls.end()) {
                user_assert(callees_1->second.find(p.func_2) == callees_1->second.end())
                    << "Invalid compute_with: there is dependency between "
                    << p.func_1 << " and " << p.func_2 << "\n";
            }
            const auto &callees_2 = indirect_calls.find(p.func_2);
            if (callees_2 != indirect_calls.end()) {
                user_assert(callees_2->second.find(p.func_1) == callees_2->second.end())
                    << "Invalid compute_with: there is dependency between "
                    << p.func_1 << " and " << p.func_2 << "\n";
            }
        }

        fuse_adjacency_list[p.func_1].insert(p.func_2);
        fuse_adjacency_list[p.func_2].insert(p.func_1);

        func_fused_pairs.push_back(p);
        graph[p.func_2].insert(p.func_1);
    }
}

vector<string> realization_order(const vector<Function> &outputs,
                                 const map<string, Function> &env) {

    // Collect all indirect calls made by all the functions in "env".
    map<string, map<string, Function>> indirect_calls;
    for (const pair<string, Function> &caller : env) {
        map<string, Function> more_funcs = find_transitive_calls(caller.second);
        indirect_calls.emplace(caller.first, std::move(more_funcs));
    }

    debug(0) << "\n";
    for (const auto &it1 : indirect_calls) {
        debug(0) << "Function calls by: " <<  it1.first << "\n";
        for (const auto &it2 : it1.second) {
            debug(0) << "  " << it2.first << "\n";
        }
    }

    // Make a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    map<string, set<string>> graph;

    // Make a DAG representing the compute_with dependencies between functions.
    // Each function maps to the list of Functions computed_with it.
    map<string, vector<FusedPair>> fused_pairs_graph;

    map<string, set<string>> fuse_adjacency_list;

    for (const pair<string, Function> &caller : env) {
        set<string> &s = graph[caller.first];
        for (const pair<string, Function> &callee : find_direct_calls(caller.second)) {
            s.insert(callee.first);
        }

        // Find all compute_with (fused) pairs. We have to look at the update
        // definitions as well since compute_with is defined per definition.
        vector<FusedPair> &func_fused_pairs = fused_pairs_graph[caller.first];
        fuse_adjacency_list[caller.first]; // Make sure every Func in 'env' is allocated a slot
        collect_fused_pairs(env, indirect_calls, caller.second.definition().schedule().fused_pairs(),
            func_fused_pairs, graph, fuse_adjacency_list);
        for (const Definition &def : caller.second.updates()) {
            collect_fused_pairs(env, indirect_calls, def.schedule().fused_pairs(),
                func_fused_pairs, graph, fuse_adjacency_list);
        }
    }

    debug(0) << "\n";
    for (const auto &i : fused_pairs_graph) {
        debug(0) << "Fused pairs of Func " << i.first << "\n";
        for (const auto &iter : i.second) {
            debug(0) << "   Func " << iter.func_1 << ".s" << iter.stage_1 << " computed before"
                     << " Func " << iter.func_2 << ".s" << iter.stage_2 << " at Var " << iter.var_name << "\n";
        }
    }

    debug(0) << "\n";
    for (const auto &i : graph) {
        debug(0) << "Callees of Func " << i.first << "\n";
        for (const auto &callee : i.second) {
            debug(0) << callee << ", ";
        }
        debug(0) << "\n";
    }

    debug(0) << "\n";
    for (const auto &i : fuse_adjacency_list) {
        debug(0) << "Neighbors of Func " << i.first << "\n";
        for (const auto &fn : i.second) {
            debug(0) << fn << ", ";
        }
        debug(0) << "\n";
    }

    // Make sure we don't have cyclic compute_with: if Func f is computed after
    // Func g, Func g should not be computed after Func f.
    for (const auto &iter : fused_pairs_graph) {
        for (const auto &pair : iter.second) {
            if (pair.func_1 == pair.func_2) {
                // compute_with among stages of a function is okay,
                // e.g. f.update(0).compute_with(f, x)
                continue;
            }
            const auto o_iter = fused_pairs_graph.find(pair.func_2);
            if (o_iter == fused_pairs_graph.end()) {
                continue;
            }
            const auto it = std::find_if(o_iter->second.begin(), o_iter->second.end(),
                [&pair](const FusedPair& other) { return (pair.func_1 == other.func_2) && (pair.func_2 == other.func_1); });
            user_assert(it == o_iter->second.end())
                << "Found cyclic dependencies between compute_with of "
                << pair.func_1 << " and " << pair.func_2 << "\n";
        }
    }

    vector<string> order;
    set<string> result_set;
    set<string> visited;

    for (Function f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, order);
        }
    }

    debug(0) << "\nREALIZATION ORDER: ";
    for (const auto &iter : order) {
        debug(0) << iter << ", ";
    }
    debug(0) << "\n";

    vector<vector<string>> fuse_group = find_fuse_group(env, fuse_adjacency_list);

    debug(0) << "\n";
    for (const auto &group : fuse_group) {
        debug(0) << "Fused group: " << "\n";
        for (const auto &fn : group) {
            debug(0) << fn << ", ";
        }
        debug(0) << "\n";
    }

    return order;
}

}
}
