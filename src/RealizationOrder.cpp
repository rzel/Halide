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

vector<string> realization_order(const vector<Function> &outputs,
                                 const map<string, Function> &env) {

    // Make a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    map<string, set<string>> graph;

    vector<FusedPair> func_fused_pairs;

    for (const pair<string, Function> &caller : env) {
        //TODO(psuriana): make sure we don't have cyclic compute_with
        {
            for (const auto &p : caller.second.definition().schedule().fused_pairs()) {
                internal_assert(std::find(func_fused_pairs.begin(), func_fused_pairs.end(), p) == func_fused_pairs.end())
                     << "Found duplicates of fused pair (" << p.func_1 << ".s" << p.stage_1 << ", "
                     << p.func_2 << ".s" << p.stage_2 << ", " << p.var_name << ")\n";
                func_fused_pairs.push_back(p);
            }
        }
        for (const Definition &def : caller.second.updates()) {
            for (const auto &p : def.schedule().fused_pairs()) {
                internal_assert(std::find(func_fused_pairs.begin(), func_fused_pairs.end(), p) == func_fused_pairs.end())
                     << "Found duplicates of fused pair (" << p.func_1 << ".s" << p.stage_1 << ", "
                     << p.func_2 << ".s" << p.stage_2 << ", " << p.var_name << ")\n";
                func_fused_pairs.push_back(p);
            }
        }

        set<string> &s = graph[caller.first];
        for (const pair<string, Function> &callee : find_direct_calls(caller.second)) {
            s.insert(callee.first);
        }
    }

    for (const auto &iter : func_fused_pairs) {
        debug(0) << "Func " << iter.func_1 << ".s" << iter.stage_1 << " computed after"
                 << " Func " << iter.func_1 << ".s" << iter.stage_2 << " at Var " << iter.var_name << "\n";
    }

    vector<string> order;
    set<string> result_set;
    set<string> visited;

    for (Function f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, order);
        }
    }

    return order;
}

}
}
