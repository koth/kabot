#include "agent/planning/task_decomposer.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[task_decomposer_tests] " << message << std::endl;
        std::exit(1);
    }
}

void TestValidateResponseAcceptsValidTaskList() {
    nlohmann::json json = {
        {"tasks", {
            {{"title", "Task A"}, {"instruction", "Do A"}},
            {{"title", "Task B"}, {"instruction", "Do B"}}
        }}
    };
    std::string error;
    Expect(kabot::agent::planning::TaskDecomposer::ValidateResponse(json, 5, error),
           "expected valid response to be accepted");
    Expect(error.empty(), "expected no error message for valid response");
}

void TestValidateResponseRejectsEmptyTitle() {
    nlohmann::json json = {
        {"tasks", {
            {{"title", ""}, {"instruction", "Do A"}}
        }}
    };
    std::string error;
    Expect(!kabot::agent::planning::TaskDecomposer::ValidateResponse(json, 5, error),
           "expected empty title to be rejected");
    Expect(!error.empty(), "expected error message for empty title");
}

void TestValidateResponseRejectsEmptyInstruction() {
    nlohmann::json json = {
        {"tasks", {
            {{"title", "Task A"}, {"instruction", ""}}
        }}
    };
    std::string error;
    Expect(!kabot::agent::planning::TaskDecomposer::ValidateResponse(json, 5, error),
           "expected empty instruction to be rejected");
    Expect(!error.empty(), "expected error message for empty instruction");
}

void TestValidateResponseRejectsTooManyTasks() {
    nlohmann::json json = {
        {"tasks", {
            {{"title", "Task 1"}, {"instruction", "Do 1"}}
        }}
    };
    std::string error;
    Expect(!kabot::agent::planning::TaskDecomposer::ValidateResponse(json, 0, error),
           "expected too many tasks to be rejected");
    Expect(!error.empty(), "expected error message for too many tasks");
}

void TestDetectCycleFindsNoneOnAcyclicGraph() {
    std::vector<kabot::agent::planning::PlannedTask> tasks;
    tasks.push_back({"A", "Do A", {}, {"B"}, {}}); // depends on B (but titles used)
    tasks.push_back({"B", "Do B", {}, {}, {}});
    std::string error;
    Expect(!kabot::agent::planning::TaskDecomposer::DetectCycle(tasks, error),
           "expected no cycle in acyclic graph");
}

void TestDetectCycleFindsCycle() {
    std::vector<kabot::agent::planning::PlannedTask> tasks;
    tasks.push_back({"A", "Do A", {}, {"B"}, {}});
    tasks.push_back({"B", "Do B", {}, {"A"}, {}});
    std::string error;
    Expect(kabot::agent::planning::TaskDecomposer::DetectCycle(tasks, error),
           "expected cycle to be detected");
    Expect(!error.empty(), "expected error message for cycle");
}

void TestTopoSortOrdersDependenciesFirst() {
    std::vector<kabot::agent::planning::PlannedTask> tasks;
    tasks.push_back({"C", "Do C", {}, {"B"}, {}});
    tasks.push_back({"A", "Do A", {}, {}, {}});
    tasks.push_back({"B", "Do B", {}, {"A"}, {}});

    auto sorted = kabot::agent::planning::TaskDecomposer::TopoSort(std::move(tasks));
    Expect(sorted.size() == 3, "expected 3 tasks after topo sort");
    Expect(sorted[0].title == "A", "expected A first");
    Expect(sorted[1].title == "B", "expected B second");
    Expect(sorted[2].title == "C", "expected C third");
}

}  // namespace

int main() {
    TestValidateResponseAcceptsValidTaskList();
    TestValidateResponseRejectsEmptyTitle();
    TestValidateResponseRejectsEmptyInstruction();
    TestValidateResponseRejectsTooManyTasks();
    TestDetectCycleFindsNoneOnAcyclicGraph();
    TestDetectCycleFindsCycle();
    TestTopoSortOrdersDependenciesFirst();
    std::cout << "task_decomposer_tests passed" << std::endl;
    return 0;
}
