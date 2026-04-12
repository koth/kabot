---
description: How to onboard an agent so it can auto-claim tasks with project and dependency context
---
This workflow helps you connect an agent to Clawhome, let it automatically claim tasks, carry project and dependency context into execution, and keep status updates informative enough for memory tracking.

## When to use this workflow

Use this workflow whenever you need to:

- connect a new agent runtime to Clawhome
- add automatic task claiming to an agent
- make an agent understand project context and dependency context
- keep agent status updates clear enough for dashboard and memory usage
- debug why an agent is idle, blocked, or not updating its state clearly

## Goal

Create an agent that:

- connects to the relay server with a valid `agentId` and token
- automatically claims the next available task
- reads task, project, and dependency context before doing work
- knows and reports what it is currently doing
- updates status in a way that can also drive memory updates
- returns to idle and continues the claim loop after finishing

## Preconditions

Before you start, confirm:

- the relay server is running
- the agent exists in server config
- the agent token is valid
- the agent can reach `/ws/agent`
- the agent is allowed to claim tasks for its configured `agentId`

## Step 1: Connect the agent

Open the websocket connection:

```text
/ws/agent?agentId=<agent-id>&token=<agent-token>
```

After connection:

- start heartbeat messages
- send an `activity.update` with `idle`
- begin the claim loop

## Step 2: Start the auto-claim loop

The agent should continuously try to claim work.

Recommended loop:

1. call `POST /api/agents/:agentId/tasks/claim-next`
2. if `found=false`, wait a short time and retry
3. if `found=true`, load the returned task payload into an execution context
4. execute the task
5. after completion, return to the top of the loop

### Claim loop rules

- do not spin aggressively when no task is available
- do not ignore dependency blocking
- do not start a second task while one is active
- if the agent has an active task already, let the server’s claim protection be the source of truth

## Step 3: Build the execution context

As soon as the agent claims a task, assemble an internal context object.

It should contain at least:

- `taskId`
- `title`
- `instruction`
- `priority`
- `sessionKey`
- `metadata`
- `project`
- `dependsOnTaskIds`
- `dependencyState`
- `blockedByTaskIds`
- `interaction`
- `waitingUser`
- `mergeRequest`

### Why this matters

The agent should not execute from instruction text alone.
It should know:

- what work it is doing
- which project the work belongs to
- which tasks it depends on
- whether the task is blocked or ready
- what output or state changes are expected

## Step 4: Report the agent’s current state

The agent should keep a clear state model.

Recommended states:

- `idle`
- `claimed`
- `running`
- `waiting_user`
- `in_review`
- `completed`
- `failed`

### State reporting rule

Every state change should be accompanied by a short human-readable summary.

Examples:

- `idle`: waiting for the next task
- `claimed`: claimed task task_xxx and preparing context
- `running`: executing task task_xxx in project default-project
- `waiting_user`: waiting for user input on task task_xxx
- `in_review`: waiting for merge review on task task_xxx
- `completed`: task task_xxx finished successfully
- `failed`: task task_xxx failed because ...

### Keep the summary stable

Do not make the summary overly verbose or random.
Prefer a short sentence that can be reused by:

- dashboard status display
- logs
- memory updates

## Step 5: Update memory at the right moments

The agent should write memory only when the event is meaningful.

### Good memory moments

- after a task is claimed
- when execution starts
- when the agent gets blocked by a dependency or external input
- when the agent reaches an important decision
- when the task finishes
- when the task fails

### Suggested memory contents

Record:

- task id
- project name / id
- dependency summary
- current state
- current goal
- important decision
- result summary
- blockers or follow-up action

### Avoid

- writing memory on every heartbeat
- duplicating the same status repeatedly
- storing large raw logs without summary
- storing state without the task or project context

## Step 6: Execute the task

During execution:

- send `activity.update` when state changes
- use `running` for active work
- use `waiting_user` if a user decision is required
- use `in_review` if the work is ready but waiting on merge or review
- send `command.result` only when the protocol flow requires it for your runtime

Even if the agent is not command-driven, keep the execution states consistent with the relay concept.

## Step 7: Finish and continue

When the task is done:

- report `completed`
- include a short result summary
- write final memory
- return to `idle`
- continue the claim loop for the next task

When the task fails:

- report `failed`
- include the failure reason
- write failure memory
- return to `idle`
- decide whether to retry or move on based on your runtime policy

## Context passing checklist

Before the agent starts executing, confirm the context includes:

- task fields
- project fields
- dependency fields
- execution helpers such as `sessionKey`
- any interaction or review details

If anything is missing, the agent should not guess silently.
It should surface a clear status and keep the context summary up to date.

## Minimal implementation pattern

A simple implementation can look like this:

1. connect websocket
2. start heartbeat
3. set status to `idle`
4. claim-next
5. if found:
   - set status to `claimed`
   - build context
   - write claim memory
   - set status to `running`
   - execute task
   - update memory on important milestones
   - set status to `completed` or `failed`
   - return to `idle`
6. if not found:
   - sleep briefly
   - retry

## Validation checklist

After implementing this workflow, validate:

- the agent can connect successfully
- the agent can claim tasks automatically
- the agent receives project context in the claimed task
- the agent receives dependency context in the claimed task
- the agent reports a clear running state
- memory updates happen on meaningful task milestones
- the agent returns to idle after finishing
- the claim loop continues for the next task

## Notes

- The server currently treats dependency satisfaction as a claim-time concern.
- The agent should still expose dependency information in its own context and status summaries.
- Good memory updates depend on the agent having a stable, explicit notion of its current task and phase.
