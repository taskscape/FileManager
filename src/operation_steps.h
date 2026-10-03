// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Host adapter that turns an SDK 105 step list into one COperations script.
// The plug-in does not reimplement move, metadata-loss, or crash recovery.

BOOL OperationStepsBlockUnload(CPluginInterfaceAbstract* plugin);

// Returns FALSE when a plan-step flag says the worker must not call the normal prompt path.
BOOL RejectPlanStepPrecondition(COperation* op, BOOL creatingDirectory, BOOL* acceptExistingDirectory, DWORD* error);

// Drops host EMetadataLoss bits the plan already accepted. salExpected uses SALMDLOSS bits.
DWORD OperationStepsFilterAcceptedLosses(DWORD hostLossMask, DWORD salExpected, BOOL accepted);
