// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// Internals of the transactional configuration store (config_store.cpp) that
// are also used by mainwnd_config.cpp and config_import.cpp; declared here so
// the clusters cannot drift apart as ad-hoc externs.

extern const char* CONFIGURATION_ACTIVE_GENERATION_REG;

BOOL OpenCommittedConfigurationGeneration(HKEY storeKey, DWORD generation, HKEY& generationKey);
BOOL BeginConfigurationTransaction(HKEY& storeKey, HKEY& generationKey, DWORD& generation);
BOOL CommitConfigurationTransaction(HKEY storeKey, HKEY generationKey, DWORD generation);
void RetirePreviousConfigurationGenerationAfterSuccessfulStartup();
