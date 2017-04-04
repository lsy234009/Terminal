﻿/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/
#include "precomp.h"

#include "CommonState.hpp"

#include "globals.h"
#include "newdelete.hpp"

#include "..\interactivity\inc\ServiceLocator.hpp"

static const SHORT s_csWindowWidth = 80;
static const SHORT s_csWindowHeight = 80;
static const SHORT s_csBufferWidth = 80;
static const SHORT s_csBufferHeight = 300;

TEXT_BUFFER_INFO* m_backupTextBufferInfo;
FontInfo* m_pFontInfo = nullptr;

CommonState::CommonState()
{
    m_heap = GetProcessHeap();
}

CommonState::~CommonState()
{
    m_heap = nullptr;
}

void CommonState::PrepareGlobalFont()
{

    COORD coordFontSize;
    coordFontSize.X = 8;
    coordFontSize.Y = 12;
    m_pFontInfo = new FontInfo(L"Consolas", 0, 0, coordFontSize, 0);
}

void CommonState::CleanupGlobalFont()
{
    if (m_pFontInfo != nullptr)
    {
        delete m_pFontInfo;
    }
}

void CommonState::PrepareGlobalScreenBuffer()
{
    COORD coordWindowSize;
    coordWindowSize.X = s_csWindowWidth;
    coordWindowSize.Y = s_csWindowHeight;
    COORD coordScreenBufferSize;
    coordScreenBufferSize.X = s_csBufferWidth;
    coordScreenBufferSize.Y = s_csBufferHeight;
    CHAR_INFO ciFill;
    ciFill.Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY;
    CHAR_INFO ciPopupFill;
    ciPopupFill.Attributes = FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED;
    UINT uiCursorSize = 12;

    SCREEN_INFORMATION::CreateInstance(coordWindowSize, m_pFontInfo, coordScreenBufferSize, ciFill, ciPopupFill, uiCursorSize, &ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer);
}

void CommonState::CleanupGlobalScreenBuffer()
{
    delete ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer;
}

void CommonState::PrepareGlobalInputBuffer()
{
    ServiceLocator::LocateGlobals()->getConsoleInformation()->pInputBuffer = new InputBuffer();
}

void CommonState::CleanupGlobalInputBuffer()
{
    delete ServiceLocator::LocateGlobals()->getConsoleInformation()->pInputBuffer;
}

void CommonState::PrepareCookedReadData()
{
    ServiceLocator::LocateGlobals()->getConsoleInformation()->lpCookedReadData = new COOKED_READ_DATA();
}

void CommonState::CleanupCookedReadData()
{
    delete ServiceLocator::LocateGlobals()->getConsoleInformation()->lpCookedReadData;
}

void CommonState::PrepareNewTextBufferInfo()
{
    COORD coordScreenBufferSize;
    coordScreenBufferSize.X = s_csBufferWidth;
    coordScreenBufferSize.Y = s_csBufferHeight;
    CHAR_INFO ciFill;
    ciFill.Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY;
    UINT uiCursorSize = 12;

    m_backupTextBufferInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;

    m_ntstatusTextBufferInfo = TEXT_BUFFER_INFO::CreateInstance(m_pFontInfo, coordScreenBufferSize, ciFill, uiCursorSize, &ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo);
}

void CommonState::CleanupNewTextBufferInfo()
{
    ASSERT(ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer != nullptr);
    delete ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;

    ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo = m_backupTextBufferInfo;
}

void FillRow(ROW* pRow)
{
    // fill a row
    // 9 characters, 6 spaces. 15 total
    PCWSTR pwszText = L"ABかかCききDE      ";
    memcpy_s(pRow->CharRow.Chars, s_csBufferWidth, pwszText, wcslen(pwszText));
    pRow->CharRow.Left = 0;
    pRow->CharRow.Right = 9; // 1 past the last valid character in the array

    // set double-byte/double-width attributes
    pRow->CharRow.KAttrs[2] = CHAR_ROW::ATTR_LEADING_BYTE;
    pRow->CharRow.KAttrs[3] = CHAR_ROW::ATTR_TRAILING_BYTE;
    pRow->CharRow.KAttrs[5] = CHAR_ROW::ATTR_LEADING_BYTE;
    pRow->CharRow.KAttrs[6] = CHAR_ROW::ATTR_TRAILING_BYTE;

    // set some colors
    TextAttribute Attr = TextAttribute(0);
    pRow->AttrRow.Initialize(15, Attr);
    // A = bright red on dark gray
    // This string starts at index 0
    Attr = TextAttribute(FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_INTENSITY);
    pRow->AttrRow.SetAttrToEnd(0, Attr);

    // BかC = dark gold on bright blue
    // This string starts at index 1
    Attr = TextAttribute(FOREGROUND_RED | FOREGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY);
    pRow->AttrRow.SetAttrToEnd(1, Attr);

    // き = bright white on dark purple
    // This string starts at index 5
    Attr = TextAttribute(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_RED | BACKGROUND_BLUE);
    pRow->AttrRow.SetAttrToEnd(5, Attr);

    // DE = black on dark green
    // This string starts at index 7
    Attr = TextAttribute(BACKGROUND_GREEN);
    pRow->AttrRow.SetAttrToEnd(7, Attr);

    // odd rows forced a wrap
    if (pRow->sRowId % 2 != 0)
    {
        pRow->CharRow.SetWrapStatus(true);
    }
    else
    {
        pRow->CharRow.SetWrapStatus(false);
    }
}

void FillBisect(ROW *pRow)
{
    // length 80 string of text with bisecting characters at the beginning and end.
    // positions of き are at 0, 27-28, 39-40, 67-68, 79
    PWCHAR pwszText = L"きABCDEFGHIJKLMNOPQRSTUVWXYZきき0123456789ききABCDEFGHIJKLMNOPQRSTUVWXYZきき0123456789き";
    memcpy_s(pRow->CharRow.Chars, s_csBufferWidth, pwszText, wcslen(pwszText));
    pRow->CharRow.Left = 0;
    pRow->CharRow.Right = 80; // 1 past the last valid character in the array

    // set double-byte/double-width attributes
    pRow->CharRow.KAttrs[0] = CHAR_ROW::ATTR_TRAILING_BYTE;
    pRow->CharRow.KAttrs[27] = CHAR_ROW::ATTR_LEADING_BYTE;
    pRow->CharRow.KAttrs[28] = CHAR_ROW::ATTR_TRAILING_BYTE;
    pRow->CharRow.KAttrs[39] = CHAR_ROW::ATTR_LEADING_BYTE;
    pRow->CharRow.KAttrs[40] = CHAR_ROW::ATTR_TRAILING_BYTE;
    pRow->CharRow.KAttrs[67] = CHAR_ROW::ATTR_LEADING_BYTE;
    pRow->CharRow.KAttrs[68] = CHAR_ROW::ATTR_TRAILING_BYTE;
    pRow->CharRow.KAttrs[79] = CHAR_ROW::ATTR_LEADING_BYTE;

    // everything gets default attributes
    pRow->AttrRow.Initialize(80, ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->GetAttributes());

    pRow->CharRow.SetWrapStatus(true);
}

typedef void(*PFNFillMethod)(ROW*);

void FillTextBufferGeneric(PFNFillMethod pfnFill, SHORT cRowsToFill)
{
    ASSERT(ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer != nullptr);
    ASSERT(ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo != nullptr);

    TEXT_BUFFER_INFO* pTextInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;

    for (SHORT iRow = 0; iRow < cRowsToFill; iRow++)
    {
        ROW* pRow = &pTextInfo->Rows[iRow];
        pfnFill(pRow);
    }

    pTextInfo->GetCursor()->SetYPosition(cRowsToFill);
}

void CommonState::FillTextBuffer()
{
    // fill with some assorted text that doesn't consume the whole row
    FillTextBufferGeneric(FillRow, 4);
}

void CommonState::FillTextBufferBisect()
{
    // fill with some text that fills the whole row and has bisecting double byte characters
    FillTextBufferGeneric(FillBisect, s_csBufferHeight);
}
