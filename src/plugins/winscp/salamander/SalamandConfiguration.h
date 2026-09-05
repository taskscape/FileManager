// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//---------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------
#include "CustomWinConfiguration.h"
//---------------------------------------------------------------------------
class CPluginInterface;
//---------------------------------------------------------------------------
// Bounds for the background queue's transfer-session count
// (ftp-improvements.md section 6.2). Four is the plan's starting point for
// trials; one is the sequential compatibility mode.
#define SALAMAND_MIN_QUEUE_TRANSFERS 1
#define SALAMAND_MAX_QUEUE_TRANSFERS 8
#define SALAMAND_DEFAULT_QUEUE_TRANSFERS 4
//---------------------------------------------------------------------------
const pcRights = 1;
const pcOwner = 2;
const pcGroup = 3;
const pcLinkTo = 4;
const pcLast = pcLinkTo;
extern const int ColumnNames[pcLast];
extern const int ColumnDescs[pcLast];
//---------------------------------------------------------------------------
class TSalamandConfiguration : public TCustomWinConfiguration
{
public:
    __fastcall TSalamandConfiguration(CPluginInterface* APlugin);

    virtual void __fastcall Default();
    virtual void __fastcall AskForMasterPasswordIfNotSet();
    void __fastcall RecryptPasswords();

    __property bool ConfirmDetach = {read = FConfirmDetach, write = FConfirmDetach};
    __property bool ConfirmUpload = {read = FConfirmUpload, write = FConfirmUpload};

    // Maximum number of concurrent transfer sessions in the background queue
    // (ftp-improvements.md section 6.2). The adapter previously set
    // TTerminalQueue::TransfersLimit to 0, which means "no limit" - a folder
    // split across many jobs could then open as many SSH sessions as it had
    // work, which most servers refuse. 1 reproduces the strictly sequential
    // behaviour and is the compatibility fallback.
    __property int QueueTransfersLimit = {read = FQueueTransfersLimit, write = SetQueueTransfersLimit};

    // When false, an ordinary copy does not walk the whole tree to compute an
    // exact total before transferring anything (section 6.4). Progress then
    // shows discovered bytes and files and an explicitly incomplete total. The
    // explicit "calculate size" command is unaffected.
    __property bool PrecalculateTransferSize = {read = FPrecalculateTransferSize, write = FPrecalculateTransferSize};
    __property AnsiString QueueViewLayout = {read = FQueueViewLayout, write = FQueueViewLayout};
    __property AnsiString PanelColumns = {read = FPanelColumns, write = FPanelColumns};
    __property int LeftColumnWidth[int Index] = {read = GetColumnWidth, write = SetColumnWidth, index = 0};
    __property int RightColumnWidth[int Index] = {read = GetColumnWidth, write = SetColumnWidth, index = 1};
    __property int LeftColumnFixedWidth[int Index] = {read = GetColumnWidth, write = SetColumnWidth, index = 2};
    __property int RightColumnFixedWidth[int Index] = {read = GetColumnWidth, write = SetColumnWidth, index = 3};

    bool IteratePanelColumns(int& Column, bool& Show, void*& Iterator);

protected:
    virtual AnsiString __fastcall ModuleFileName();

    virtual void __fastcall SaveData(THierarchicalStorage* Storage, bool All);
    virtual void __fastcall LoadData(THierarchicalStorage* Storage);
    virtual bool __fastcall GetUseMasterPassword();
    virtual AnsiString __fastcall StronglyRecryptPassword(AnsiString Password, AnsiString Key);
    virtual AnsiString __fastcall DecryptPassword(AnsiString Password, AnsiString Key);

    __property AnsiString LeftColumnsWidths = {read = GetColumnsWidths, write = SetColumnsWidths, index = 0};
    __property AnsiString RightColumnsWidths = {read = GetColumnsWidths, write = SetColumnsWidths, index = 1};
    __property AnsiString LeftColumnsFixedWidths = {read = GetColumnsWidths, write = SetColumnsWidths, index = 2};
    __property AnsiString RightColumnsFixedWidths = {read = GetColumnsWidths, write = SetColumnsWidths, index = 3};

private:
    // Clamps the stored value so a corrupted profile cannot ask for an
    // unbounded or zero number of transfer sessions.
    void __fastcall SetQueueTransfersLimit(int value);

    CPluginInterface* FPlugin;
    bool FConfirmDetach;
    bool FConfirmUpload;
    int FQueueTransfersLimit;
    bool FPrecalculateTransferSize;
    AnsiString FQueueViewLayout;
    AnsiString FPanelColumns;
    int FColumnsWidths[4][pcLast];

    int __fastcall GetColumnWidth(int Index, int Group);
    void __fastcall SetColumnWidth(int Index, int Group, int Value);
    AnsiString __fastcall GetColumnsWidths(int Group);
    void __fastcall SetColumnsWidths(int Group, AnsiString Value);
};
//---------------------------------------------------------------------------
#define SalamandConfiguration (dynamic_cast<TSalamandConfiguration*>(::Configuration))
//---------------------------------------------------------------------------
