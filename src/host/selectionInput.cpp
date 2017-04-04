/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"

#include "search.h"

#include "..\interactivity\inc\ServiceLocator.hpp"

// Routine Description:
// - Handles a keyboard event for extending the current selection
// - Must be called when the console is in selecting state.
// Arguments:
// - pInputKeyInfo : The key press state information from the keyboard
// Return Value:
// - True if the event is handled. False otherwise.
Selection::KeySelectionEventResult Selection::HandleKeySelectionEvent(_In_ const INPUT_KEY_INFO* const pInputKeyInfo)
{
    ASSERT(IsInSelectingState());

    const WORD wVirtualKeyCode = pInputKeyInfo->GetVirtualKey();

    // if escape or ctrl-c, cancel selection
    if (!IsMouseButtonDown())
    {
        if (wVirtualKeyCode == VK_ESCAPE)
        {
            ClearSelection();
            return Selection::KeySelectionEventResult::EventHandled;
        }
        else if (wVirtualKeyCode == VK_RETURN ||
                 ((ServiceLocator::LocateInputServices()->GetKeyState(VK_CONTROL) & KEY_PRESSED) &&
                  (wVirtualKeyCode == 'C' || // Ctrl-c
                   wVirtualKeyCode == VK_INSERT))) // Ctrl-INS
        {
            Telemetry::Instance().SetKeyboardTextEditingUsed();

            // copy selection
            return Selection::KeySelectionEventResult::CopyToClipboard;
        }
        else if (ServiceLocator::LocateGlobals()->getConsoleInformation()->GetEnableColorSelection() &&
                 ('0' <= wVirtualKeyCode) &&
                 ('9' >= wVirtualKeyCode))
        {
            if (_HandleColorSelection(pInputKeyInfo))
            {
                return Selection::KeySelectionEventResult::EventHandled;
            }
        }
    }

    if (!IsMouseInitiatedSelection())
    {
        if (_HandleMarkModeSelectionNav(pInputKeyInfo))
        {
            return Selection::KeySelectionEventResult::EventHandled;
        }
    }
    else if (!IsMouseButtonDown())
    {
        // if the existing selection is a line selection
        if (IsLineSelection())
        {
            // try to handle it first if we've used a valid keyboard command to extend the selection
            if (HandleKeyboardLineSelectionEvent(pInputKeyInfo))
            {
                return Selection::KeySelectionEventResult::EventHandled;
            }
        }

        // if in mouse selection mode and user hits a key, cancel selection
        if (!IsSystemKey(wVirtualKeyCode)) {
            ClearSelection();
        }
    }

    return Selection::KeySelectionEventResult::EventNotHandled;
}

// Routine Description:
// - Checks if a keyboard event can be handled by HandleKeyboardLineSelectionEvent
// Arguments:
// - pInputKeyInfo : The key press state information from the keyboard
// Return Value:
// - True if the event can be handled. False otherwise.
// NOTE:
// - Keyboard handling cases in this function should be synchronized with HandleKeyboardLineSelectionEvent
bool Selection::s_IsValidKeyboardLineSelection(_In_ const INPUT_KEY_INFO* const pInputKeyInfo)
{
    bool fIsValidCombination = false;

    const WORD wVirtualKeyCode = pInputKeyInfo->GetVirtualKey();

    if (pInputKeyInfo->IsShiftOnly())
    {
        switch (wVirtualKeyCode)
        {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NEXT:
        case VK_PRIOR:
        case VK_HOME:
        case VK_END:
            fIsValidCombination = true;
        }
    }
    else if (pInputKeyInfo->IsShiftAndCtrlOnly())
    {
        switch (wVirtualKeyCode)
        {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
            fIsValidCombination = true;
        }
    }

    return fIsValidCombination;
}

// Routine Description:
// - Modifies the given selection point to the edge of the next (or previous) word.
// - By default operates in a left-to-right fashion.
// Arguments:
// - fReverse: Specifies that this function should operate in reverse. E.g. Right-to-left.
// - srectEdges: The edges of the current screen buffer. All values are valid positions within the screen buffer.
// - coordAnchor: The point within the buffer (inside the edges) where this selection started.
// - coordSelPoint: Defines selection region from coordAnchor to this point. Modified to define the new selection region.
// Return Value:
// - <none>
void Selection::WordByWordSelection(_In_ const bool fReverse,
                                    _In_ const SMALL_RECT srectEdges,
                                    _In_ const COORD coordAnchor,
                                    _Inout_ COORD *pcoordSelPoint) const
{
    TEXT_BUFFER_INFO* const pTextInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;

    // first move one character in the requested direction
    if (!fReverse)
    {
        Utils::s_DoIncrementScreenCoordinate(srectEdges, pcoordSelPoint);
    }
    else
    {
        Utils::s_DoDecrementScreenCoordinate(srectEdges, pcoordSelPoint);
    }

    // get the character at the new position
    ROW *pRow = pTextInfo->GetRowByOffset(pcoordSelPoint->Y);
    WCHAR wchTest = pRow->CharRow.Chars[pcoordSelPoint->X];

    // we want to go until the state change from delim to non-delim
    bool fCurrIsDelim = IS_WORD_DELIM(wchTest);
    bool fPrevIsDelim;

    // find the edit-line boundaries that we can highlight
    COORD coordMaxLeft;
    COORD coordMaxRight;
    const bool fSuccess = s_GetInputLineBoundaries(&coordMaxLeft, &coordMaxRight);

    // if line boundaries fail, then set them to the buffer corners so they don't restrict anything.
    if (!fSuccess)
    {
        coordMaxLeft.X = srectEdges.Left;
        coordMaxLeft.Y = srectEdges.Top;

        coordMaxRight.X = srectEdges.Right;
        coordMaxRight.Y = srectEdges.Bottom;
    }

    // track whether we failed to move during an operation
    // if we failed to move, we hit the end of the buffer and should just highlight to there and be done.
    bool fMoveSucceeded = false;

    // determine if we're highlighting more text or unhighlighting already selected text.
    bool fUnhighlighting;
    if (!fReverse)
    {
        // if the selection point is left of the anchor, then we're unhighlighting when moving right
        fUnhighlighting = Utils::s_CompareCoords(*pcoordSelPoint, coordAnchor) < 0;
    }
    else
    {
        // if the selection point is right of the anchor, then we're unhighlighting when moving left
        fUnhighlighting = Utils::s_CompareCoords(*pcoordSelPoint, coordAnchor) > 0;
    }

    do
    {
        // store previous state
        fPrevIsDelim = fCurrIsDelim;

        // to make us "sticky" within the edit line, stop moving once we've reached a given max position left/right
        // users can repeat the command to move past the line and continue word selecting
        // if we're at the max position left, stop moving
        if (Utils::s_CompareCoords(*pcoordSelPoint, coordMaxLeft) == 0)
        {
            // set move succeeded to false as we can't move any further
            fMoveSucceeded = false;
            break;
        }

        // if we're at the max position right, stop moving.
        // we don't want them to "word select" past the end of the edit line as there's likely nothing there.
        // (thus >= and not == like left)
        if (Utils::s_CompareCoords(*pcoordSelPoint, coordMaxRight) >= 0)
        {
            // set move succeeded to false as we can't move any further.
            fMoveSucceeded = false;
            break;
        }

        if (!fReverse)
        {
            fMoveSucceeded = Utils::s_DoIncrementScreenCoordinate(srectEdges, pcoordSelPoint);
        }
        else
        {
            fMoveSucceeded = Utils::s_DoDecrementScreenCoordinate(srectEdges, pcoordSelPoint);
        }

        if (!fMoveSucceeded)
        {
            break;
        }

        // get the character associated with the new position
        pRow = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo->GetRowByOffset(pcoordSelPoint->Y);
        ASSERT(pRow != nullptr);
        __analysis_assume(pRow != nullptr);
        wchTest = pRow->CharRow.Chars[pcoordSelPoint->X];

        fCurrIsDelim = IS_WORD_DELIM(wchTest);

        // This is a bit confusing.
        // If we're going Left to Right (!fReverse)...
        // - Then we want to keep going UNTIL (!) we move from a delimiter (fPrevIsDelim) to a normal character (!fCurrIsDelim)
        //   This will then eat up all delimiters after a word and stop once we reach the first letter of the next word.
        // If we're going Right to Left (fReverse)...
        // - Then we want to keep going UNTIL (!) we move from a normal character (!fPrevIsDelim) to a delimeter (fCurrIsDelim)
        //   This will eat up all letters of the word and stop once we see the delimiter before the word.
    } while (!fReverse ? !(fPrevIsDelim && !fCurrIsDelim) : !(!fPrevIsDelim && fCurrIsDelim));

    // To stop the loop, we had to move the cursor one too far to figure out that the delta occurred from delimeter to not (or vice versa)
    // Therefore move back by one character after proceeding through the loop.
    // EXCEPT:
    // 1. If we broke out of the loop by reaching the beginning of the buffer, leave it alone.
    // 2. If we're un-highlighting a region, also leave it alone.
    //    This is an oddity that occurs because our cursor is on a character, not between two characters like most text editors.
    //    We want the current position to be ON the first letter of the word (or the last delimeter after the word) so it stays highlighted.
    if (fMoveSucceeded && !fUnhighlighting)
    {
        if (!fReverse)
        {
            fMoveSucceeded = Utils::s_DoDecrementScreenCoordinate(srectEdges, pcoordSelPoint);
        }
        else
        {
            fMoveSucceeded = Utils::s_DoIncrementScreenCoordinate(srectEdges, pcoordSelPoint);
        }

        ASSERT(fMoveSucceeded); // we should never fail to move forward after having moved backward
    }
}

// Routine Description:
// - Handles a keyboard event for manipulating line-mode selection with the keyboard
// - If called when console isn't in selecting state, will start a new selection.
// Arguments:
// - inputKeyInfo : The key press state information from the keyboard
// Return Value:
// - True if the event is handled. False otherwise.
// NOTE:
// - Keyboard handling cases in this function should be synchronized with IsValidKeyboardLineSelection
bool Selection::HandleKeyboardLineSelectionEvent(_In_ const INPUT_KEY_INFO* const pInputKeyInfo)
{
    const WORD wVirtualKeyCode = pInputKeyInfo->GetVirtualKey();

    // if this isn't a valid key combination for this function, exit quickly.
    if (!s_IsValidKeyboardLineSelection(pInputKeyInfo))
    {
        return false;
    }

    Telemetry::Instance().SetKeyboardTextSelectionUsed();

    // if we're not currently selecting anything, start a new mouse selection
    if (!IsInSelectingState())
    {
        InitializeMouseSelection(ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo->GetCursor()->GetPosition());

        // force that this is a line selection
        _AlignAlternateSelection(true);

        ShowSelection();

        // if we did shift+left/right, then just exit
        if (pInputKeyInfo->IsShiftOnly())
        {
            switch (wVirtualKeyCode)
            {
            case VK_LEFT:
            case VK_RIGHT:
                return true;
            }
        }
    }

    // anchor is the first clicked position
    const COORD coordAnchor = _coordSelectionAnchor;

    // rect covers the entire selection
    const SMALL_RECT rectSelection = _srSelectionRect;

    // the selection point is the other corner of the rectangle from the anchor that we're about to manipulate
    COORD coordSelPoint;
    coordSelPoint.X = coordAnchor.X == rectSelection.Left ? rectSelection.Right : rectSelection.Left;
    coordSelPoint.Y = coordAnchor.Y == rectSelection.Top ? rectSelection.Bottom : rectSelection.Top;

    // this is the maximum size of the buffer
    SMALL_RECT srectEdges;
    Utils::s_GetCurrentBufferEdges(&srectEdges);

    const SHORT sWindowHeight = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->GetScreenWindowSizeY();

    ASSERT(coordSelPoint.X >= srectEdges.Left && coordSelPoint.X <= srectEdges.Right);
    ASSERT(coordSelPoint.Y >= srectEdges.Top && coordSelPoint.Y <= srectEdges.Bottom);

    // retrieve input line information. If we are selecting from within the input line, we need
    // to bound ourselves within the input data first and not move into the back buffer.

    COORD coordInputLineStart;
    COORD coordInputLineEnd;
    bool fHaveInputLine = s_GetInputLineBoundaries(&coordInputLineStart, &coordInputLineEnd);

    if (pInputKeyInfo->IsShiftOnly())
    {
        switch (wVirtualKeyCode)
        {
            // shift + left/right extends the selection by one character, wrapping at screen edge
        case VK_LEFT:
        {
            Utils::s_DoDecrementScreenCoordinate(srectEdges, &coordSelPoint);
            break;
        }
        case VK_RIGHT:
        {
            Utils::s_DoIncrementScreenCoordinate(srectEdges, &coordSelPoint);

            const TEXT_BUFFER_INFO* const pTextInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;
            const ROW* const pRow = pTextInfo->GetRowByOffset(coordSelPoint.Y);
            const BYTE bAttr = pRow->CharRow.KAttrs[coordSelPoint.X];

            // if we're about to split a character in half, keep moving right
            if (bAttr & CHAR_ROW::ATTR_TRAILING_BYTE)
            {
                Utils::s_DoIncrementScreenCoordinate(srectEdges, &coordSelPoint);
            }
            break;
        }
            // shift + up/down extends the selection by one row, stopping at top or bottom of screen
        case VK_UP:
        {
            if (coordSelPoint.Y > srectEdges.Top)
            {
                coordSelPoint.Y--;
            }
            break;
        }
        case VK_DOWN:
        {
            if (coordSelPoint.Y < srectEdges.Bottom)
            {
                coordSelPoint.Y++;
            }
            break;
        }
            // shift + pgup/pgdn extends selection up or down one full screen
        case VK_NEXT:
        {
            coordSelPoint.Y += sWindowHeight; // TODO: potential overflow
            if (coordSelPoint.Y > srectEdges.Bottom)
            {
                coordSelPoint.Y = srectEdges.Bottom;
            }
            break;
        }
        case VK_PRIOR:
        {
            coordSelPoint.Y -= sWindowHeight; // TODO: potential underflow
            if (coordSelPoint.Y < srectEdges.Top)
            {
                coordSelPoint.Y = srectEdges.Top;
            }
            break;
        }
            // shift + home/end extends selection to beginning or end of line
        case VK_HOME:
        {
            /*
            Prompt sample:
                qwertyuiopasdfg
                C:\>dir /p /w C
                :\windows\syste
                m32

                The input area runs from the d in "dir" to the space after the 2 in "32"

                We want to stop the HOME command from running to the beginning of the line only
                if we're on the first input line because then it would capture the prompt.

                So if the selection point we're manipulating is currently anywhere in the
                "dir /p /w C" area, then pressing home should only move it on top of the "d" in "dir".

                But if it's already at the "d" in dir, pressing HOME again should move us to the
                beginning of the line anyway to collect up the prompt as well.
            */

            // if we're in the input line
            if (fHaveInputLine)
            {
                // and the selection point is inside the input line area
                if (Utils::s_CompareCoords(coordSelPoint, coordInputLineStart) > 0)
                {
                    // and we're on the same line as the beginning of the input
                    if (coordInputLineStart.Y == coordSelPoint.Y)
                    {
                        // then only back up to the start of the input
                        coordSelPoint.X = coordInputLineStart.X;
                        break;
                    }
                }
            }

            // otherwise, fall through and select to the head of the line.
            coordSelPoint.X = 0;
            break;
        }
        case VK_END:
        {
            /*
            Prompt sample:
                qwertyuiopasdfg
                C:\>dir /p /w C
                :\windows\syste
                m32

                The input area runs from the d in "dir" to the space after the 2 in "32"

                We want to stop the END command from running to the space after the "32" because
                that's just where the cursor lies to let more text get entered and not actually
                a valid selection area.

                So if the selection point is anywhere on the "m32", pressing end should move it
                to on top of the "2".

                Additionally, if we're starting within the output buffer (qwerty, etc. and C:\>), then
                pressing END should stop us before we enter the input line the first time.

                So if we're anywhere on "C:\", we should select up to the ">" character and no further
                until a subsequent press of END.

                At the subsequent press of END when we're on the ">", we should move to the end of the input
                line or the end of the screen, whichever comes first.
            */

            // if we're in the input line
            if (fHaveInputLine)
            {
                // and the selection point is inside the input area
                if (Utils::s_CompareCoords(coordSelPoint, coordInputLineStart) >= 0)
                {
                    // and we're on the same line as the end of the input
                    if (coordInputLineEnd.Y == coordSelPoint.Y)
                    {
                        // and we're not already on the end of the input...
                        if (coordSelPoint.X < coordInputLineEnd.X)
                        {
                            // then only use end to the end of the input
                            coordSelPoint.X = coordInputLineEnd.X;
                            break;
                        }
                    }
                }
                else
                {
                    // otherwise if we're outside and on the same line as the start of the input
                    if (coordInputLineStart.Y == coordSelPoint.Y)
                    {
                        // calculate the end of the outside/output buffer position
                        const short sEndOfOutputPos = coordInputLineStart.X - 1;

                        // if we're not already on the very last character...
                        if (coordSelPoint.X < sEndOfOutputPos)
                        {
                            // then only move to just before the beginning of the input
                            coordSelPoint.X = sEndOfOutputPos;
                            break;
                        }
                        else if (coordSelPoint.X == sEndOfOutputPos)
                        {
                            // if we were on the last character,
                            // then if the end of the input line is also on this current line,
                            // move to that.
                            if (coordSelPoint.Y == coordInputLineEnd.Y)
                            {
                                coordSelPoint.X = coordInputLineEnd.X;
                                break;
                            }
                        }
                    }
                }
            }

            // otherwise, fall through and go to selecting the whole line to the end.
            coordSelPoint.X = srectEdges.Right;
            break;
        }
        }
    }
    else if (pInputKeyInfo->IsShiftAndCtrlOnly())
    {
        switch (wVirtualKeyCode)
        {
            // shift + ctrl + left/right extends selection to next/prev word boundary
        case VK_LEFT:
        {
            WordByWordSelection(true, srectEdges, coordAnchor, &coordSelPoint);
            break;
        }
        case VK_RIGHT:
        {
            WordByWordSelection(false, srectEdges, coordAnchor, &coordSelPoint);
            break;
        }
            // shift + ctrl + up/down does the same thing that shift + up/down does
        case VK_UP:
        {
            if (coordSelPoint.Y > srectEdges.Top)
            {
                coordSelPoint.Y--;
            }
            break;
        }
        case VK_DOWN:
        {
            if (coordSelPoint.Y < srectEdges.Bottom)
            {
                coordSelPoint.Y++;
            }
            break;
        }
            // shift + ctrl + home/end extends selection to top or bottom of buffer from selection
        case VK_HOME:
        {
            COORD coordValidStart;
            GetValidAreaBoundaries(&coordValidStart, nullptr);
            coordSelPoint = coordValidStart;
            break;
        }
        case VK_END:
        {
            COORD coordValidEnd;
            GetValidAreaBoundaries(nullptr, &coordValidEnd);
            coordSelPoint = coordValidEnd;
            break;
        }
        }
    }

    // ensure we're not planting the cursor in the middle of a double-wide character.
    const TEXT_BUFFER_INFO* const pTextInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;
    ROW* const pRow = pTextInfo->GetRowByOffset(coordSelPoint.Y);
    ASSERT(pRow != nullptr);
    __analysis_assume(pRow != nullptr);
    BYTE bAttr = pRow->CharRow.KAttrs[coordSelPoint.X];

    if (bAttr & CHAR_ROW::ATTR_TRAILING_BYTE)
    {
        // try to move off by highlighting the lead half too.
        bool fSuccess = Utils::s_DoDecrementScreenCoordinate(srectEdges, &coordSelPoint);

        // if that fails, move off to the next character
        if (!fSuccess)
        {
            Utils::s_DoIncrementScreenCoordinate(srectEdges, &coordSelPoint);
        }
    }

    ExtendSelection(coordSelPoint);

    return true;
}

// Routine Description:
// - Checks whether the ALT key was pressed when this method was called.
// - ALT is the modifier for the alternate selection mode, so this will set state accordingly.
// Arguments:
// - <none> (Uses global key state)
// Return Value:
// - <none>
void Selection::CheckAndSetAlternateSelection()
{
    _fUseAlternateSelection = !!(ServiceLocator::LocateInputServices()->GetKeyState(VK_MENU) & KEY_PRESSED);
}

// Routine Description:
// - Handles a keyboard event for manipulating color selection
// - If called when console isn't in selecting state, will start a new selection.
// Arguments:
// - pInputKeyInfo : The key press state information from the keyboard
// Return Value:
// - True if the event is handled. False otherwise.
bool Selection::_HandleColorSelection(_In_ const INPUT_KEY_INFO* const pInputKeyInfo)
{
    SMALL_RECT* const psrSelection = &_srSelectionRect;
    const WORD wVirtualKeyCode = pInputKeyInfo->GetVirtualKey();

    //  It's a numeric key,  a text mode buffer and the color selection regkey is set,
    //  then check to see if the user want's to color the selection or search and
    //  highlight the selection.
    bool fAltPressed = pInputKeyInfo->IsAltPressed();
    bool fShiftPressed = pInputKeyInfo->IsShiftPressed();
    bool fCtrlPressed = false;

    //  Shift implies a find-and-color operation.  We only support finding a string,  not
    //  a block.  So if the selected area is > 1 line in height,  just ignore the shift
    //  and color the selection.  Also ignore if there is no current selection.
    if ((fShiftPressed) && (!IsAreaSelected() || (psrSelection->Top != psrSelection->Bottom)))
    {
        fShiftPressed = false;
    }

    //  If CTRL + ALT together,  then we interpret as ALT (eg on French
    //  keyboards AltGr == RALT+LCTRL,  but we want it to behave as ALT).
    if (!fAltPressed)
    {
        fCtrlPressed = pInputKeyInfo->IsCtrlPressed();
    }

    SCREEN_INFORMATION* const pScreenInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer;

    //  Clip the selection to within the console buffer
    pScreenInfo->ClipToScreenBuffer(psrSelection);

    //  If ALT or CTRL are pressed,  then color the selected area.
    //  ALT+n => fg,  CTRL+n => bg
    if (fAltPressed || fCtrlPressed)
    {
        ULONG ulAttr = wVirtualKeyCode - '0' + 6;

        if (fCtrlPressed)
        {
            //  Setting background color.  Set fg color to black.
            ulAttr <<= 4;
        }
        else
        {
            // Set foreground color. Maintain the current console bg color.
            ulAttr |= ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->GetAttributes().GetLegacyAttributes() & 0xf0;
        }

        // If shift was pressed as well, then this is actually a
        // find-and-color request. Otherwise just color the selection.
        if (fShiftPressed)
        {
            SCREEN_INFORMATION* pScreenInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer;

            ULONG cLength = psrSelection->Right - psrSelection->Left + 1;
            if (cLength > SEARCH_STRING_LENGTH)
            {
                cLength = SEARCH_STRING_LENGTH;
            }

            // Pull the selection out of the buffer to pass to the
            // search function. Clamp to max search string length.
            // We just copy the bytes out of the row buffer.
            const ROW* pRow = pScreenInfo->TextInfo->GetRowByOffset(psrSelection->Top);

            ASSERT(pRow != nullptr);
            __analysis_assume(pRow != nullptr);

            WCHAR pwszSearchString[SEARCH_STRING_LENGTH + 1];
            memmove(pwszSearchString, &pRow->CharRow.Chars[psrSelection->Left], cLength * sizeof(WCHAR));

            pwszSearchString[cLength] = L'\0';

            // Clear the selection and call the search / mark function.
            ClearSelection();

            SearchForString(pScreenInfo, pwszSearchString, (USHORT)cLength, TRUE, FALSE, TRUE, ulAttr, nullptr);
        }
        else
        {
            ColorSelection(psrSelection, ulAttr);
            ClearSelection();
        }

        return true;
    }

    return false;
}

// Routine Description:
// - Handles a keyboard event for selection in mark mode
// Arguments:
// - pInputKeyInfo : The key press state information from the keyboard
// Return Value:
// - True if the event is handled. False otherwise.
bool Selection::_HandleMarkModeSelectionNav(_In_ const INPUT_KEY_INFO* const pInputKeyInfo)
{
    const WORD wVirtualKeyCode = pInputKeyInfo->GetVirtualKey();

    // we're selecting via keyboard -- handle keystrokes
    if (wVirtualKeyCode == VK_RIGHT ||
        wVirtualKeyCode == VK_LEFT ||
        wVirtualKeyCode == VK_UP ||
        wVirtualKeyCode == VK_DOWN ||
        wVirtualKeyCode == VK_NEXT ||
        wVirtualKeyCode == VK_PRIOR ||
        wVirtualKeyCode == VK_END ||
        wVirtualKeyCode == VK_HOME)
    {
        SCREEN_INFORMATION* const pScreenInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer;
        TEXT_BUFFER_INFO* const pTextInfo = pScreenInfo->TextInfo;
        BYTE bKAttrs;
        SHORT iNextRightX;
        SHORT iNextLeftX = 0;

        const COORD cursorPos = pTextInfo->GetCursor()->GetPosition();
        ROW* const pRow = pTextInfo->GetRowByOffset(cursorPos.Y);


        bKAttrs = pRow->CharRow.KAttrs[cursorPos.X];
        if (bKAttrs & CHAR_ROW::ATTR_LEADING_BYTE)
        {
            iNextRightX = 2;
        }
        else
        {
            iNextRightX = 1;
        }

        if (cursorPos.X > 0)
        {
            bKAttrs = pRow->CharRow.KAttrs[cursorPos.X - 1];
            if (bKAttrs & CHAR_ROW::ATTR_TRAILING_BYTE)
            {
                iNextLeftX = 2;
            }
            else if (bKAttrs & CHAR_ROW::ATTR_LEADING_BYTE)
            {
                if (cursorPos.X - 1 > 0)
                {
                    bKAttrs = pRow->CharRow.KAttrs[cursorPos.X - 2];
                    if (bKAttrs & CHAR_ROW::ATTR_TRAILING_BYTE)
                    {
                        iNextLeftX = 3;
                    }
                    else
                    {
                        iNextLeftX = 2;
                    }
                }
                else
                {
                    iNextLeftX = 1;
                }
            }
            else
            {
                iNextLeftX = 1;
            }
        }
        Cursor* pCursor = pTextInfo->GetCursor();
        switch (wVirtualKeyCode)
        {
        case VK_RIGHT:
        {
            if (cursorPos.X + iNextRightX < pScreenInfo->GetScreenBufferSize().X)
            {
                pCursor->IncrementXPosition(iNextRightX);
            }
            break;
        }

        case VK_LEFT:
        {
            if (cursorPos.X > 0)
            {
                pCursor->DecrementXPosition(iNextLeftX);
            }
            break;
        }

        case VK_UP:
        {
            if (cursorPos.Y > 0)
            {
                pCursor->DecrementYPosition(1);
            }
            break;
        }

        case VK_DOWN:
        {
            if (cursorPos.Y + 1 < pScreenInfo->GetScreenBufferSize().Y)
            {
                pCursor->IncrementYPosition(1);
            }
            break;
        }

        case VK_NEXT:
        {
            pCursor->IncrementYPosition(pScreenInfo->GetScreenWindowSizeY() - 1);
            const COORD coordBufferSize = pScreenInfo->GetScreenBufferSize();
            if (pCursor->GetPosition().Y >= coordBufferSize.Y)
            {
                pCursor->SetYPosition(coordBufferSize.Y - 1);
            }
            break;
        }

        case VK_PRIOR:
        {
            pCursor->DecrementYPosition(pScreenInfo->GetScreenWindowSizeY() - 1);
            if (pCursor->GetPosition().Y < 0)
            {
                pCursor->SetYPosition(0);
            }
            break;
        }

        case VK_END:
        {
            // End by itself should go to end of current line. Ctrl-End should go to end of buffer.
            pCursor->SetXPosition(pScreenInfo->GetScreenBufferSize().X - 1);

            if (pInputKeyInfo->IsCtrlPressed())
            {
                COORD coordValidEnd;
                GetValidAreaBoundaries(nullptr, &coordValidEnd);

                // Adjust Y position of cursor to the final line with valid text
                pCursor->SetYPosition(coordValidEnd.Y);
            }
            break;
        }

        case VK_HOME:
        {
            // Home by itself should go to the beginning of the current line. Ctrl-Home should go to the beginning of
            // the buffer
            pCursor->SetXPosition(0);

            if (pInputKeyInfo->IsCtrlPressed())
            {
                pCursor->SetYPosition(0);
            }
            break;
        }

        default:
            ASSERT(FALSE);
        }

        // see if shift is down. if so, we're extending the selection. otherwise, we're resetting the anchor
        if (ServiceLocator::LocateInputServices()->GetKeyState(VK_SHIFT) & KEY_PRESSED)
        {
            // if we're just starting to "extend" our selection from moving around as a cursor
            // then attempt to set the alternate selection state based on the ALT key right now
            if (!IsAreaSelected())
            {
                CheckAndSetAlternateSelection();
            }

            ExtendSelection(pCursor->GetPosition());
        }
        else
        {
            // if the selection was not empty, reset the anchor
            if (IsAreaSelected())
            {
                HideSelection();
                _dwSelectionFlags &= ~CONSOLE_SELECTION_NOT_EMPTY;
                _fUseAlternateSelection = false;
            }

            pCursor->SetHasMoved(TRUE);
            _coordSelectionAnchor = pTextInfo->GetCursor()->GetPosition();
            pScreenInfo->MakeCursorVisible(_coordSelectionAnchor);
            _srSelectionRect.Left = _srSelectionRect.Right = _coordSelectionAnchor.X;
            _srSelectionRect.Top = _srSelectionRect.Bottom = _coordSelectionAnchor.Y;
        }
        return true;
    }

    return false;
}

#pragma region Calculation/Support for keyboard selection

// Routine Description:
// - Retrieves the boundaries of the input line (first and last char positions)
// Arguments:
// - pcoordInputStart - Position of the first character in the input line
// - pcoordInputEnd - Position of the last character in the input line
// Return Value:
// - If true, the boundaries returned are valid. If false, they should be discarded.
_Check_return_ _Success_(return)
bool Selection::s_GetInputLineBoundaries(_Out_opt_ COORD* const pcoordInputStart, _Out_opt_ COORD* const pcoordInputEnd)
{
    SMALL_RECT srectEdges;
    Utils::s_GetCurrentBufferEdges(&srectEdges);

    const COOKED_READ_DATA* const pCookedReadData = ServiceLocator::LocateGlobals()->getConsoleInformation()->lpCookedReadData;
    const TEXT_BUFFER_INFO* const pTextInfo = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo;

    // if we have no read data, we have no input line
    if (pCookedReadData == nullptr || pCookedReadData->_NumberOfVisibleChars <= 0)
    {
        return false;
    }

    const COORD coordStart = pCookedReadData->_OriginalCursorPosition;
    COORD coordEnd = pCookedReadData->_OriginalCursorPosition;

    if (coordEnd.X < 0 && coordEnd.Y < 0)
    {
        // if the original cursor position from the input line data is invalid, then the buffer cursor position is the final position
        coordEnd = pTextInfo->GetCursor()->GetPosition();
    }
    else
    {
        // otherwise, we need to add the number of characters in the input line to the original cursor position
        Utils::s_AddToPosition(srectEdges, pCookedReadData->_NumberOfVisibleChars, &coordEnd);
    }

    // - 1 so the coordinate is on top of the last position of the text, not one past it.
    Utils::s_AddToPosition(srectEdges, -1, &coordEnd);

    if (pcoordInputStart != nullptr)
    {
        pcoordInputStart->X = coordStart.X;
        pcoordInputStart->Y = coordStart.Y;
    }

    if (pcoordInputEnd != nullptr)
    {
        pcoordInputEnd->X = coordEnd.X;
        pcoordInputEnd->Y = coordEnd.Y;
    }

    return true;
}

// Routine Description:
// - Gets the boundaries of all valid text on the screen.
//   Includes the output/back buffer as well as the input line text.
// Arguments:
// - pcoordInputStart - Position of the first character in the buffer
// - pcoordInputEnd - Position of the last character in the buffer
// Return Value:
// - If true, the boundaries returned are valid. If false, they should be discarded.
void Selection::GetValidAreaBoundaries(_Out_opt_ COORD* const pcoordValidStart, _Out_opt_ COORD* const pcoordValidEnd) const
{
    COORD coordEnd;
    coordEnd.X = 0;
    coordEnd.Y = 0;

    const bool fHaveInput = s_GetInputLineBoundaries(nullptr, &coordEnd);

    if (!fHaveInput)
    {
        if (IsInSelectingState() && IsKeyboardMarkSelection())
        {
            coordEnd = _coordSavedCursorPosition;
        }
        else
        {
            coordEnd = ServiceLocator::LocateGlobals()->getConsoleInformation()->CurrentScreenBuffer->TextInfo->GetCursor()->GetPosition();
        }
    }

    if (pcoordValidStart != nullptr)
    {
        // valid area always starts at 0,0
        pcoordValidStart->X = 0;
        pcoordValidStart->Y = 0;
    }

    if (pcoordValidEnd != nullptr)
    {
        pcoordValidEnd->X = coordEnd.X;
        pcoordValidEnd->Y = coordEnd.Y;
    }
}

// Routine Description:
// - Determines if a coordinate lies between the start and end positions
// - NOTE: Is inclusive of the edges of the boundary.
// Arguments:
// - coordPosition - The position to test
// - coordFirst - The start or left most edge of the regional boundary.
// - coordSecond - The end or right most edge of the regional boundary.
// Return Value:
// - True if it's within the bounds (inclusive). False otherwise.
bool Selection::s_IsWithinBoundaries(_In_ const COORD coordPosition, _In_ const COORD coordStart, _In_ const COORD coordEnd)
{
    bool fInBoundaries = false;

    if (Utils::s_CompareCoords(coordStart, coordPosition) <= 0)
    {
        if (Utils::s_CompareCoords(coordPosition, coordEnd) <= 0)
        {
            fInBoundaries = true;
        }
    }

    return fInBoundaries;
}

#pragma endregion
