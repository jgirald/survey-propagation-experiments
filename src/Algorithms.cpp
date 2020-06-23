#include <algorithm>
#include <cmath>
#include <vector>

// Project headers
#include <Algorithms.hpp>
#include <Configuration.hpp>
#include <Utils.hpp>

namespace sat {

// -----------------------------------------------------------------------------
// Survey Propagation
// -----------------------------------------------------------------------------
SPResult SurveyPropagation(FactorGraph* graph) {
  // 1 - Random initialization of survey values
  std::vector<Edge*> edges = graph->GetEnabledEdges();
  for (Edge* edge : edges) {
    edge->survey = utils::RandomGen::getRandomReal01();
  }

  // 4 - Repeat until all surveys converge or max iterations are reached
  bool allEdgesConverged = false;
  uint totalIt = 0;
  for (uint i = 0; i < SP_MAX_ITERATIONS && !allEdgesConverged; i++) {
    // 2 - Order randomly the set of edges
    std::shuffle(edges.begin(), edges.end(), utils::RandomGen::randomGenerator);

    // 3 - Update the survey value of each edge
    allEdgesConverged = true;
    for (Edge* edge : edges) {
      double previousSurveyValue = edge->survey;
      UpdateSurvey(edge);

      // Check if edge converged
      bool hasConverged =
          std::abs(edge->survey - previousSurveyValue) < SP_EPSILON;
      if (!hasConverged) allEdgesConverged = false;
    }

    totalIt += 1;
  }

  return {allEdgesConverged, totalIt};
}

void UpdateSurvey(Edge* ai) {
  // Param edge is a->i
  double Sai = 1.0;

  // For each a->j when j != i
  for (Edge* aj : ai->clause->GetEnabledEdges()) {
    if (aj == ai) continue;  // j == i

    // Product values initalization for all b->j survey values
    double Pubj = 1.0;
    double Psbj = 1.0;
    double P0bj = 1.0;

    // For each b->j when b != a
    for (Edge* bj : aj->variable->GetEnabledEdges()) {
      if (bj == aj) continue;  // b == a

      if (bj->type != aj->type) {
        // Update Pubj if both edges have different edge type -> b€Vua(j)
        Pubj = Pubj * (1.0 - bj->survey);
      }
      if (bj->type == aj->type) {
        // Update Psbj if both edges have same edge type -> b€Vsa(j)
        Psbj = Psbj * (1.0 - bj->survey);
      }

      P0bj = P0bj * (1.0 - bj->survey);
    }

    // Product values for all a->j survey values (Equation 26)
    double Puaj = (1.0 - Pubj) * Psbj;
    double Psaj = (1.0 - Psbj) * Pubj;
    double P0aj = P0bj;

    // Update a->i survey value (Equation 27)
    double div = Puaj / (Puaj + Psaj + P0aj);

    // Some survey may result in a 0/0 division. To avoid errors, the result
    // is estblished to 0 to make the survey trivial
    if (std::isnan(div)) {
      Sai = 0.0;
      break;
    } else {
      Sai = Sai * div;
    }
  }

  // update a->i survey
  ai->survey = Sai;
}

// -----------------------------------------------------------------------------
// Unit Propagation
// -----------------------------------------------------------------------------
bool UnitPropagation(FactorGraph* graph) {
  // Run until contradiction is found or no unit clauses are found
  while (true) {
    // 1. Found all enabled Clauses with only one enabled Edge
    std::vector<Clause*> unitClauses;
    for (Clause* clause : graph->GetEnabledClauses()) {
      if (clause->GetEnabledEdges().size() == 1) unitClauses.push_back(clause);
    }

    // Return true if no unitary Clauses are found.
    if (unitClauses.size() == 0) return true;

    // Assign the Variable of the unit clause to true if Edge is POSITIVE
    // and to false if NEGATIVE.
    for (Clause* unitClause : unitClauses) {
      Edge* edge = unitClause->GetEnabledEdges()[0];
      if (!edge->variable->assigned) {
        edge->variable->AssignValue(edge->type);
        // std::cout << "UP assign: " << edge->variable->id << " - " <<
        // edge->type << std::endl;
      }
      // If the variable is already assigned with a value distinct from the edge
      // type, return false (contradiction found)
      else {
        if (edge->type != edge->variable->value) return false;
      }
    }

    // 2. For each Clause in the graph:
    for (Clause* clause : graph->GetEnabledClauses()) {
      for (Edge* edge : clause->GetEnabledEdges()) {
        if (edge->variable->assigned) {
          // 2.1 Disable the clause if is satisfied by the assignment
          // (contains the assigned literal)
          if (edge->type == edge->variable->value) {
            clause->Dissable();
            break;
          }

          // 2.2 Disable each Edge of the clause that contain an assigned
          // Variable with the oposite literal type.
          else {
            edge->Dissable();
          }
        }
      }

      // If the Clause is enabled and have 0 enabled Edges,
      // return false (contradiction found).
      if (clause->enabled && clause->GetEnabledEdges().size() == 0)
        return false;
    }

    // std::cout << "UP:" << graph << std::endl;
  }
}

// -----------------------------------------------------------------------------
// Walksat
// -----------------------------------------------------------------------------
bool Walksat(FactorGraph* graph) {
  // TODO Check that the parameters have correct values

  // 1. For try t = 0 to maxTries
  for (uint t = 0; t < WS_MAX_TRIES; t++) {
    // 1.1 Assign all Varibles with random values
    for (Variable* var : graph->GetUnassignedVariables()) {
      var->AssignValue(utils::RandomGen::getRandomBool());
    }

    // 1.2 For flip f = 0 to maxFlips:
    for (uint f = 0; f < WS_MAX_FLIPS; f++) {
      // 1.2.1 If FactorGraph is satisfied, return true
      if (graph->IsSAT()) return true;

      // 1.2.2 Randomly select an unsatisfied clause and calculate the
      // break-count of its variables

      // Separate clauses into sat and unsat
      std::vector<Clause*> satClauses;
      std::vector<Clause*> unsatClauses;
      for (Clause* clause : graph->GetEnabledClauses()) {
        if (clause->IsSAT())
          satClauses.push_back(clause);
        else
          unsatClauses.push_back(clause);
      }

      // Select random unsat clause
      std::uniform_int_distribution<> randomInt(0, unsatClauses.size() - 1);
      int randIndex = randomInt(utils::RandomGen::randomGenerator);
      Clause* selectedClause = unsatClauses[randIndex];
      std::vector<Edge*> selectedClauseEdges =
          selectedClause->GetEnabledEdges();

      // Calculate break-count (number of currently satisfied clauses
      // that become unsatisfied if the variable value is fliped) of variables
      // in selected clause
      Variable* lowestBreakCountVar = nullptr;
      uint lowestBreakCount = satClauses.size() + 1;
      for (Edge* edge : selectedClauseEdges) {
        uint breakCount = 0;
        // Flip variable and count
        edge->variable->AssignValue(!edge->variable->value);
        for (Clause* satClause : satClauses) {
          if (!satClause->IsSAT()) breakCount++;
        }
        // flip again to stay as it was
        edge->variable->AssignValue(!edge->variable->value);

        // Update lowest break-count
        if (lowestBreakCountVar == nullptr || breakCount < lowestBreakCount) {
          lowestBreakCountVar = edge->variable;
          lowestBreakCount = breakCount;
        }

        // If break-count = 0 no need to count more
        if (breakCount == 0) break;
      }

      // 1.2.3 Flip a Variable of the Clause if has break-count = 0
      // If not, with probability p (noise), flip a random variable and
      // with probability 1 - p, flip the variable with lowest break-count
      if (lowestBreakCount == 0) {
        lowestBreakCountVar->AssignValue(!lowestBreakCountVar->value);
      } else {
        // probability 1 - p
        if (utils::RandomGen::getRandomReal01() > WS_NOISE) {
          lowestBreakCountVar->AssignValue(!lowestBreakCountVar->value);
        }
        // probability p
        else {
          std::uniform_int_distribution<> randEdgeIndexDist(
              0, selectedClauseEdges.size() - 1);
          int randomEdgeIndex =
              randEdgeIndexDist(utils::RandomGen::randomGenerator);
          Variable* var = selectedClauseEdges[randomEdgeIndex]->variable;
          var->AssignValue(!var->value);
        }
      }
    }
  }

  // 2. If a sat assignment was not found, return false.
  return false;
}

// -----------------------------------------------------------------------------
// Survey Inspired Decimation
// -----------------------------------------------------------------------------
SIDResult SID(FactorGraph* graph, double fraction) {
  // Start metrics
  uint totalSPIt = 0;
  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();
  while (true) {
    // 1. Run SP. If does not converge return false.
    SPResult spResult = SurveyPropagation(graph);
    totalSPIt += spResult.iterations;
    if (!spResult.converged) {
      std::chrono::steady_clock::time_point end =
          std::chrono::steady_clock::now();
      std::cout << "SP don't converge" << std::endl;
      return {false, totalSPIt, begin, end};
    }

    // 2. Decimate
    // 2.2 If all surveys are trivial, return WALKSAT result
    bool allTrivial = true;
    for (Edge* edge : graph->GetEnabledEdges()) {
      if (edge->survey != 0.0) {
        allTrivial = false;
        break;
      }
    }
    if (allTrivial) {
      bool walksatResult = Walksat(graph);
      std::chrono::steady_clock::time_point end =
          std::chrono::steady_clock::now();
      if (!walksatResult)
        std::cout << "Walksat can't found a solution" << std::endl;
      else {
        std::cout << "Solved with Walksat" << std::endl;
      }
      return {walksatResult, totalSPIt, begin, end};
    }

    // 2.1 Otherwise, evaluate all variables, assign a set of them and clean
    // the graph
    std::vector<Variable*> unassignedVariables =
        graph->GetUnassignedVariables();

    for (Variable* variable : unassignedVariables) {
      EvaluateVariable(variable);
      // std::cout << variable->id << "(" << variable->evalValue << ")"
      //           << std::endl;
    }

    // Assign minimum 1 variable
    int assignFraction = (int)(unassignedVariables.size() * fraction);
    if (assignFraction < 1) assignFraction = 1;
    std::sort(unassignedVariables.begin(), unassignedVariables.end(),
              [](const Variable* lvar, const Variable* rvar) {
                return std::abs(lvar->evalValue) > std::abs(rvar->evalValue);
              });

    for (int i = 0; i < assignFraction; i++) {
      Variable* var = unassignedVariables[i];
      bool newValue = var->evalValue > 0;
      // std::cout << "Assigned: X" << var->id << " - "
      //           << (newValue ? "true" : "false") << std::endl;
      var->AssignValue(newValue);
      for (Edge* edge : var->GetEnabledEdges()) {
        if (edge->type == var->value) {
          edge->clause->Dissable();
        } else {
          edge->Dissable();
        }
      }
    }

    // 4. Run UNIT PROPAGTION
    bool UPResult = UnitPropagation(graph);
    // If a contradiction in found, return false
    if (!UPResult) {
      std::chrono::steady_clock::time_point end =
          std::chrono::steady_clock::now();
      std::cout << "UP found a contradiction" << std::endl;
      return {false, totalSPIt, begin, end};
    }
    // If SAT, return true.
    if (graph->IsSAT()) {
      std::chrono::steady_clock::time_point end =
          std::chrono::steady_clock::now();
      std::cout << "Solved with UP" << std::endl;
      return {true, totalSPIt, begin, end};
    }
  };
}

void EvaluateVariable(Variable* variable) {
  // Vi  = V(i)  -> Subset of clauses where the variable i appears
  // ViP = V+(i) -> substed of V(i) where i appears unnegated
  // ViN = V-(i) -> substed of V(i) where i appears negated
  // Product values initialization for all a->i survey values
  double PVi0 = 1.0;
  double PViP = 1.0;
  double PViN = 1.0;

  // For each a->i
  for (Edge* ai : variable->GetEnabledEdges()) {
    if (ai->type) {
      // Update PViP if variable i appears unnegated in clause a
      PViP = PViP * (1.0 - ai->survey);
    } else {
      // Update PViN if variable i appears negated in clause a
      PViN = PViN * (1.0 - ai->survey);
    }

    PVi0 = PVi0 * (1.0 - ai->survey);
  }

  // Auxiliar variables to calculate Wi(+) and Wi(-)
  double PiP = (1.0 - PViP) * PViN;
  double PiN = (1.0 - PViN) * PViP;
  double Pi0 = PVi0;

  // Calculate 'biases'
  double WiP = PiP / (PiP + PiN + Pi0);  // Wi(+)
  double WiN = PiN / (PiP + PiN + Pi0);  // Wi(-)

  if (std::isnan(WiP) || std::isnan(WiN))
    variable->evalValue = 0.0;
  else
    variable->evalValue = WiP - WiN;
}

}  // namespace sat