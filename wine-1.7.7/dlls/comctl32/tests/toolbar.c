/* Unit tests for toolbar.
 *
 * Copyright 2005 Krzysztof Foltman
 * Copyright 2007 Mikolaj Zalewski
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "commctrl.h" 

#include "resources.h"

#include "wine/test.h"

#include "msg.h"

#define PARENT_SEQ_INDEX       0
#define NUM_MSG_SEQUENCES      1

static struct msg_sequence *sequences[NUM_MSG_SEQUENCES];

static HWND hMainWnd;
static BOOL g_fBlockHotItemChange;
static BOOL g_fReceivedHotItemChange;
static BOOL g_fExpectedHotItemOld;
static BOOL g_fExpectedHotItemNew;
static DWORD g_dwExpectedDispInfoMask;
static BOOL g_ResetDispTextPtr;

static const struct message ttgetdispinfo_parent_seq[] = {
    { WM_NOTIFY, sent|id, 0, 0, TBN_GETINFOTIPA },
    /* next line is todo, currently TTN_GETDISPINFOW is raised here */
    { WM_NOTIFY, sent|id, 0, 0, TTN_GETDISPINFOA },
    { 0 }
};

#define DEFINE_EXPECT(func) \
    static BOOL expect_ ## func = FALSE, called_ ## func = FALSE

#define CHECK_EXPECT2(func) \
    do { \
        ok(expect_ ##func, "unexpected call " #func "\n"); \
        called_ ## func = TRUE; \
    }while(0)

#define CHECK_CALLED(func) \
    do { \
        ok(called_ ## func, "expected " #func "\n"); \
        expect_ ## func = called_ ## func = FALSE; \
    }while(0)

#define SET_EXPECT(func) \
    expect_ ## func = TRUE

#define expect(EXPECTED,GOT) ok((GOT)==(EXPECTED), "Expected %d, got %d\n", (EXPECTED), (GOT))

#define check_rect(name, val, exp, ...) ok(val.top == exp.top && val.bottom == exp.bottom && \
    val.left == exp.left && val.right == exp.right, "invalid rect (%d,%d) (%d,%d) - expected (%d,%d) (%d,%d) - (" name ")\n", \
    val.left, val.top, val.right, val.bottom, exp.left, exp.top, exp.right, exp.bottom, __VA_ARGS__);
 
#define compare(val, exp, format) ok((val) == (exp), #val " value " format " expected " format "\n", (val), (exp));

#define check_button_size(handle, width, height, ...) {\
    LRESULT bsize = SendMessageA(handle, TB_GETBUTTONSIZE, 0, 0);\
    ok(bsize == MAKELONG(width, height), "Unexpected button size - got size (%d, %d), expected (%d, %d)\n", LOWORD(bsize), HIWORD(bsize), width, height);\
    }

static void MakeButton(TBBUTTON *p, int idCommand, int fsStyle, int nString) {
  p->iBitmap = -2;
  p->idCommand = idCommand;
  p->fsState = TBSTATE_ENABLED;
  p->fsStyle = fsStyle;
  p->iString = nString;
}

static LRESULT parent_wnd_notify(LPARAM lParam)
{
    NMHDR *hdr = (NMHDR *)lParam;
    NMTBHOTITEM *nmhi;
    NMTBDISPINFO *nmdisp;
    switch (hdr->code)
    {
        case TBN_HOTITEMCHANGE:
            nmhi = (NMTBHOTITEM *)lParam;
            g_fReceivedHotItemChange = TRUE;
            if (g_fExpectedHotItemOld != g_fExpectedHotItemNew)
            {
                compare(nmhi->idOld, g_fExpectedHotItemOld, "%d");
                compare(nmhi->idNew, g_fExpectedHotItemNew, "%d");
            }
            if (g_fBlockHotItemChange)
                return 1;
            break;

        case TBN_GETDISPINFOA:
            ok(FALSE, "TBN_GETDISPINFOA received\n");
            break;

        case TBN_GETINFOTIPA:
        {
            NMTBGETINFOTIPA *tbgit = (NMTBGETINFOTIPA*)lParam;

            if (g_ResetDispTextPtr)
            {
                tbgit->pszText = NULL;
                return 0;
            }
            break;
        }
        case TBN_GETDISPINFOW:
            nmdisp = (NMTBDISPINFOA *)lParam;

            compare(nmdisp->dwMask, g_dwExpectedDispInfoMask, "%x");
            ok(nmdisp->pszText == NULL, "pszText is not NULL\n");
        break;
    }
    return 0;
}

static LRESULT CALLBACK parent_wnd_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static LONG defwndproc_counter = 0;
    struct message msg;
    LRESULT ret;

    msg.message = message;
    msg.flags = sent|wparam|lparam;
    if (defwndproc_counter) msg.flags |= defwinproc;
    msg.wParam = wParam;
    msg.lParam = lParam;
    if (message == WM_NOTIFY && lParam) msg.id = ((NMHDR*)lParam)->code;

    /* log system messages, except for painting */
    if (message < WM_USER &&
        message != WM_PAINT &&
        message != WM_ERASEBKGND &&
        message != WM_NCPAINT &&
        message != WM_NCHITTEST &&
        message != WM_GETTEXT &&
        message != WM_GETICON &&
        message != WM_DEVICECHANGE)
    {
        trace("parent: %p, %04x, %08lx, %08lx\n", hWnd, message, wParam, lParam);
        add_message(sequences, PARENT_SEQ_INDEX, &msg);
    }

    switch (message)
    {
        case WM_NOTIFY:
            return parent_wnd_notify(lParam);
    }

    defwndproc_counter++;
    ret = DefWindowProcA(hWnd, message, wParam, lParam);
    defwndproc_counter--;

    return ret;
}

static void basic_test(void)
{
    TBBUTTON buttons[9];
    HWND hToolbar;
    int i;

    for (i=0; i<9; i++)
        MakeButton(buttons+i, 1000+i, TBSTYLE_CHECKGROUP, 0);
    MakeButton(buttons+3, 1003, TBSTYLE_SEP|TBSTYLE_GROUP, 0);
    MakeButton(buttons+6, 1006, TBSTYLE_SEP, 0);

    hToolbar = CreateToolbarEx(hMainWnd,
        WS_VISIBLE | WS_CLIPCHILDREN | CCS_TOP |
        WS_CHILD | TBSTYLE_LIST,
        100,
        0, NULL, 0,
        buttons, sizeof(buttons)/sizeof(buttons[0]),
        0, 0, 20, 16, sizeof(TBBUTTON));
    ok(hToolbar != NULL, "Toolbar creation\n");
    SendMessage(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)"test\000");

    /* test for exclusion working inside a separator-separated :-) group */
    SendMessage(hToolbar, TB_CHECKBUTTON, 1000, 1); /* press A1 */
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1000, 0), "A1 pressed\n");
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1001, 0), "A2 not pressed\n");

    SendMessage(hToolbar, TB_CHECKBUTTON, 1004, 1); /* press A5, release A1 */
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1004, 0), "A5 pressed\n");
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1000, 0), "A1 not pressed anymore\n");

    SendMessage(hToolbar, TB_CHECKBUTTON, 1005, 1); /* press A6, release A5 */
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1005, 0), "A6 pressed\n");
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1004, 0), "A5 not pressed anymore\n");

    /* test for inter-group crosstalk, ie. two radio groups interfering with each other */
    SendMessage(hToolbar, TB_CHECKBUTTON, 1007, 1); /* press B2 */
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1005, 0), "A6 still pressed, no inter-group crosstalk\n");
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1000, 0), "A1 still not pressed\n");
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1007, 0), "B2 pressed\n");

    SendMessage(hToolbar, TB_CHECKBUTTON, 1000, 1); /* press A1 and ensure B group didn't suffer */
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1005, 0), "A6 not pressed anymore\n");
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1000, 0), "A1 pressed\n");
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1007, 0), "B2 still pressed\n");

    SendMessage(hToolbar, TB_CHECKBUTTON, 1008, 1); /* press B3, and ensure A group didn't suffer */
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1005, 0), "A6 pressed\n");
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1000, 0), "A1 pressed\n");
    ok(!SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1007, 0), "B2 not pressed\n");
    ok(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 1008, 0), "B3 pressed\n");

    /* tests with invalid index */
    compare(SendMessage(hToolbar, TB_ISBUTTONCHECKED, 0xdeadbeef, 0), -1L, "%ld");
    compare(SendMessage(hToolbar, TB_ISBUTTONPRESSED, 0xdeadbeef, 0), -1L, "%ld");
    compare(SendMessage(hToolbar, TB_ISBUTTONENABLED, 0xdeadbeef, 0), -1L, "%ld");
    compare(SendMessage(hToolbar, TB_ISBUTTONINDETERMINATE, 0xdeadbeef, 0), -1L, "%ld");
    compare(SendMessage(hToolbar, TB_ISBUTTONHIGHLIGHTED, 0xdeadbeef, 0), -1L, "%ld");
    compare(SendMessage(hToolbar, TB_ISBUTTONHIDDEN, 0xdeadbeef, 0), -1L, "%ld");

    DestroyWindow(hToolbar);
}

static void rebuild_toolbar(HWND *hToolbar)
{
    if (*hToolbar)
        DestroyWindow(*hToolbar);
    *hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        hMainWnd, (HMENU)5, GetModuleHandle(NULL), NULL);
    ok(*hToolbar != NULL, "Toolbar creation problem\n");
    ok(SendMessage(*hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0) == 0, "TB_BUTTONSTRUCTSIZE failed\n");
    ok(SendMessage(*hToolbar, TB_AUTOSIZE, 0, 0) == 0, "TB_AUTOSIZE failed\n");
    ok(SendMessage(*hToolbar, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FONT), 0)==1, "WM_SETFONT\n");
}

static void rebuild_toolbar_with_buttons(HWND *hToolbar)
{
    TBBUTTON buttons[5];
    rebuild_toolbar(hToolbar);
    
    ZeroMemory(&buttons, sizeof(buttons));
    buttons[0].idCommand = 1;
    buttons[0].fsStyle = BTNS_BUTTON;
    buttons[0].fsState = TBSTATE_ENABLED;
    buttons[0].iString = -1;
    buttons[1].idCommand = 3;
    buttons[1].fsStyle = BTNS_BUTTON;
    buttons[1].fsState = TBSTATE_ENABLED;
    buttons[1].iString = -1;
    buttons[2].idCommand = 5;
    buttons[2].fsStyle = BTNS_SEP;
    buttons[2].fsState = TBSTATE_ENABLED;
    buttons[2].iString = -1;
    buttons[3].idCommand = 7;
    buttons[3].fsStyle = BTNS_BUTTON;
    buttons[3].fsState = TBSTATE_ENABLED;
    buttons[3].iString = -1;
    buttons[4].idCommand = 9;
    buttons[4].fsStyle = BTNS_BUTTON;
    buttons[4].fsState = 0;  /* disabled */
    buttons[4].iString = -1;
    ok(SendMessage(*hToolbar, TB_ADDBUTTONS, 5, (LPARAM)buttons) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessage(*hToolbar, TB_AUTOSIZE, 0, 0) == 0, "TB_AUTOSIZE failed\n");
}

static void add_128x15_bitmap(HWND hToolbar, int nCmds)
{
    TBADDBITMAP bmp128;
    bmp128.hInst = GetModuleHandle(NULL);
    bmp128.nID = IDB_BITMAP_128x15;
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, nCmds, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
}

#define CHECK_IMAGELIST(count, dx, dy) { \
    int cx, cy; \
    HIMAGELIST himl = (HIMAGELIST)SendMessageA(hToolbar, TB_GETIMAGELIST, 0, 0); \
    ok(himl != NULL, "No image list\n"); \
    if (himl != NULL) {\
        ok(ImageList_GetImageCount(himl) == count, "Images count mismatch - %d vs %d\n", count, ImageList_GetImageCount(himl)); \
        ImageList_GetIconSize(himl, &cx, &cy); \
        ok(cx == dx && cy == dy, "Icon size mismatch - %dx%d vs %dx%d\n", dx, dy, cx, cy); \
    } \
}

static void test_add_bitmap(void)
{
    HWND hToolbar = NULL;
    TBADDBITMAP bmp128;
    TBADDBITMAP bmp80;
    TBADDBITMAP stdsmall;
    TBADDBITMAP addbmp;
    HIMAGELIST himl;
    INT ret;

    /* empty 128x15 bitmap */
    bmp128.hInst = GetModuleHandle(NULL);
    bmp128.nID = IDB_BITMAP_128x15;

    /* empty 80x15 bitmap */
    bmp80.hInst = GetModuleHandle(NULL);
    bmp80.nID = IDB_BITMAP_80x15;

    /* standard bitmap - 240x15 pixels */
    stdsmall.hInst = HINST_COMMCTRL;
    stdsmall.nID = IDB_STD_SMALL_COLOR;

    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 8, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(8, 16, 16);
    
    /* adding more bitmaps */
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp80) == 8, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(13, 16, 16);
    /* adding the same bitmap will simply return the index of the already loaded block */
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 8, (LPARAM)&bmp128);
    ok(ret == 0, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(13, 16, 16);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp80);
    ok(ret == 8, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(13, 16, 16);
    /* even if we increase the wParam */
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 55, (LPARAM)&bmp80);
    ok(ret == 8, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(13, 16, 16);

    /* when the wParam is smaller than the bitmaps count but non-zero, all the bitmaps will be added*/
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 3, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(8, 16, 16);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp80);
    ok(ret == 3, "TB_ADDBITMAP - unexpected return %d\n", ret);
    /* the returned value is misleading - id 8 is the id of the first icon from bmp80 */
    CHECK_IMAGELIST(13, 16, 16);

    /* the same for negative wParam */
    rebuild_toolbar(&hToolbar);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, -143, (LPARAM)&bmp128);
    ok(ret == 0, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(8, 16, 16);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&bmp80);
    ok(ret == -143, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(13, 16, 16);

    /* for zero only one bitmap will be added */
    rebuild_toolbar(&hToolbar);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 0, (LPARAM)&bmp80);
    ok(ret == 0, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(1, 16, 16);

    /* if wParam is larger than the amount of icons, the list is grown */
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 100, (LPARAM)&bmp80) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(100, 16, 16);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 100, (LPARAM)&bmp128);
    ok(ret == 100, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(200, 16, 16);

    /* adding built-in items - the wParam is ignored */
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp80) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(5, 16, 16);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 0, (LPARAM)&stdsmall) == 5, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(20, 16, 16);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp128) == 20, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(28, 16, 16);

    /* when we increase the bitmap size, less icons will be created */
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(20, 20)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(6, 20, 20);
    ret = SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&bmp80);
    ok(ret == 1, "TB_ADDBITMAP - unexpected return %d\n", ret);
    CHECK_IMAGELIST(10, 20, 20);
    /* the icons can be resized - an UpdateWindow is needed as this probably happens during WM_PAINT */
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(8, 8)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(26, 8, 8);
    /* loading a standard bitmaps automatically resizes the icons */
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&stdsmall) == 2, "TB_ADDBITMAP - unexpected return\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(28, 16, 16);

    /* two more SETBITMAPSIZE tests */
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 100, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(100, 16, 16);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 100, (LPARAM)&bmp80) == 100, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(200, 16, 16);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(8, 8)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(200, 8, 8);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(30, 30)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(200, 30, 30);
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 5, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(8, 16, 16);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 3, (LPARAM)&bmp80) == 5, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(13, 16, 16);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(30, 30)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(8, 30, 30);
    /* when the width or height is zero, set it to 1 */
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(0, 0)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(208, 1, 1);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(0, 5)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(208, 1, 5);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(5, 0)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(41, 5, 1);

    /* the control can add bitmaps to an existing image list */
    rebuild_toolbar(&hToolbar);
    himl = ImageList_LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_80x15), 20, 2, CLR_NONE, IMAGE_BITMAP, LR_DEFAULTCOLOR);
    ok(himl != NULL, "failed to create imagelist\n");
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl) == 0, "TB_SETIMAGELIST failed\n");
    CHECK_IMAGELIST(4, 20, 15);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&bmp128) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(10, 20, 15);
    /* however TB_SETBITMAPSIZE/add std bitmap won't change the image size (the button size does change) */
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(8, 8)) == TRUE, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(15, 14), "%x");
    CHECK_IMAGELIST(10, 20, 15);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 0, (LPARAM)&stdsmall) == 1, "TB_SETBITMAPSIZE failed\n");
    UpdateWindow(hToolbar);
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(23, 22), "%x");
    CHECK_IMAGELIST(22, 20, 15);

    /* check standard bitmaps */
    addbmp.hInst = HINST_COMMCTRL;
    addbmp.nID = IDB_STD_SMALL_COLOR;
    rebuild_toolbar(&hToolbar);
    ImageList_Destroy(himl);

    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(15, 16, 16);
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(23, 22), "%x");
    addbmp.nID = IDB_STD_LARGE_COLOR;
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(15, 24, 24);
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(31, 30), "%x");

    addbmp.nID = IDB_VIEW_SMALL_COLOR;
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(12, 16, 16);
    addbmp.nID = IDB_VIEW_LARGE_COLOR;
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(12, 24, 24);

    addbmp.nID = IDB_HIST_SMALL_COLOR;
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(5, 16, 16);
    addbmp.nID = IDB_HIST_LARGE_COLOR;
    rebuild_toolbar(&hToolbar);
    ok(SendMessageA(hToolbar, TB_ADDBITMAP, 1, (LPARAM)&addbmp) == 0, "TB_ADDBITMAP - unexpected return\n");
    CHECK_IMAGELIST(5, 24, 24);


    DestroyWindow(hToolbar);
}

#define CHECK_STRING_TABLE(count, tab) { \
        INT _i; \
        CHAR _buf[260]; \
        for (_i = 0; _i < (count); _i++) {\
            ret = SendMessageA(hToolbar, TB_GETSTRING, MAKEWPARAM(260, _i), (LPARAM)_buf); \
            ok(ret >= 0, "TB_GETSTRING - unexpected return %d while checking string %d\n", ret, _i); \
            if (ret >= 0) \
                ok(strcmp(_buf, (tab)[_i]) == 0, "Invalid string #%d - '%s' vs '%s'\n", _i, (tab)[_i], _buf); \
        } \
        ok(SendMessageA(hToolbar, TB_GETSTRING, MAKEWPARAM(260, (count)), (LPARAM)_buf) == -1, \
            "Too many strings in table\n"); \
    }

static void test_add_string(void)
{
    LPCSTR test1 = "a\0b\0";
    LPCSTR test2 = "|a|b||\0";
    LPCSTR ret1[] = {"a", "b"};
    LPCSTR ret2[] = {"a", "b", "|a|b||"};
    LPCSTR ret3[] = {"a", "b", "|a|b||", "p", "q"};
    LPCSTR ret4[] = {"a", "b", "|a|b||", "p", "q", "p"};
    LPCSTR ret5[] = {"a", "b", "|a|b||", "p", "q", "p", "p", "q"};
    LPCSTR ret6[] = {"a", "b", "|a|b||", "p", "q", "p", "p", "q", "p", "", "q"};
    LPCSTR ret7[] = {"a", "b", "|a|b||", "p", "q", "p", "p", "q", "p", "", "q", "br", "c", "d"};
    HWND hToolbar = NULL;
    TBBUTTON button;
    int ret;
    CHAR buf[260];

    rebuild_toolbar(&hToolbar);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)test1);
    ok(ret == 0, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    ret = SendMessageA(hToolbar, TB_GETSTRING, MAKEWPARAM(260, 1), (LPARAM)buf);
    if (ret == 0)
    {
        win_skip("TB_GETSTRING needs 5.80\n");
        return;
    }
    CHECK_STRING_TABLE(2, ret1);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)test2);
    ok(ret == 2, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(3, ret2);

    /* null instance handle */
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, 0, IDS_TBADD1);
    ok(ret == -1, "TB_ADDSTRINGA - unexpected return %d\n", ret);

    /* invalid instance handle */
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, 0xdeadbeef, IDS_TBADD1);
    ok(ret == -1, "TB_ADDSTRINGA - unexpected return %d\n", ret);

    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD1);
    ok(ret == 3, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(3, ret2);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD2);
    ok(ret == 3, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(5, ret3);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD3);
    ok(ret == 5, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(6, ret4);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD4);
    ok(ret == 6, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(8, ret5);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD5);
    ok(ret == 8, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(11, ret6);
    ret = SendMessageA(hToolbar, TB_ADDSTRINGA, (WPARAM)GetModuleHandle(NULL), IDS_TBADD7);
    ok(ret == 11, "TB_ADDSTRINGA - unexpected return %d\n", ret);
    CHECK_STRING_TABLE(14, ret7);

    ZeroMemory(&button, sizeof(button));
    button.iString = (UINT_PTR)"Test";
    SendMessageA(hToolbar, TB_INSERTBUTTONA, 0, (LPARAM)&button);
    CHECK_STRING_TABLE(14, ret7);
    SendMessageA(hToolbar, TB_ADDBUTTONSA, 1, (LPARAM)&button);
    CHECK_STRING_TABLE(14, ret7);

    DestroyWindow(hToolbar);
}

static void expect_hot_notify(int idold, int idnew)
{
    g_fExpectedHotItemOld = idold;
    g_fExpectedHotItemNew = idnew;
    g_fReceivedHotItemChange = FALSE;
}

#define check_hot_notify() \
    ok(g_fReceivedHotItemChange, "TBN_HOTITEMCHANGE not received\n"); \
    g_fExpectedHotItemOld = g_fExpectedHotItemNew = 0;

static void test_hotitem(void)
{
    HWND hToolbar = NULL;
    TBBUTTONINFO tbinfo;
    LRESULT ret;

    g_fBlockHotItemChange = FALSE;

    rebuild_toolbar_with_buttons(&hToolbar);
    /* set TBSTYLE_FLAT. comctl5 allows hot items only for such toolbars.
     * comctl6 doesn't have this requirement even when theme == NULL */
    SetWindowLong(hToolbar, GWL_STYLE, TBSTYLE_FLAT | GetWindowLong(hToolbar, GWL_STYLE));
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == -1, "Hot item: %ld, expected -1\n", ret);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 1, 0);
    ok(ret == -1, "TB_SETHOTITEM returned %ld, expected -1\n", ret);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 1, "Hot item: %ld, expected 1\n", ret);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 2, 0);
    ok(ret == 1, "TB_SETHOTITEM returned %ld, expected 1\n", ret);

    ret = SendMessage(hToolbar, TB_SETHOTITEM, 0xbeef, 0);
    ok(ret == 2, "TB_SETHOTITEM returned %ld, expected 2\n", ret);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 2, "Hot item: %lx, expected 2\n", ret);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, -0xbeef, 0);
    ok(ret == 2, "TB_SETHOTITEM returned %ld, expected 2\n", ret);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == -1, "Hot item: %lx, expected -1\n", ret);

    expect_hot_notify(0, 7);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 3, 0);
    ok(ret == -1, "TB_SETHOTITEM returned %ld, expected -1\n", ret);
    check_hot_notify();
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 3, "Hot item: %lx, expected 3\n", ret);
    g_fBlockHotItemChange = TRUE;
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 2, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 2\n", ret);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 3, "Hot item: %lx, expected 3\n", ret);
    g_fBlockHotItemChange = FALSE;

    g_fReceivedHotItemChange = FALSE;
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 0xbeaf, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 3\n", ret);
    ok(g_fReceivedHotItemChange == FALSE, "TBN_HOTITEMCHANGE received for invalid parameter\n");

    g_fReceivedHotItemChange = FALSE;
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 3, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 3\n", ret);
    ok(g_fReceivedHotItemChange == FALSE, "TBN_HOTITEMCHANGE received after a duplication\n");

    expect_hot_notify(7, 0);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, -0xbeaf, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 3\n", ret);
    check_hot_notify();
    SendMessage(hToolbar, TB_SETHOTITEM, 3, 0);

    /* setting disabled buttons will generate a notify with the button id but no button will be hot */
    expect_hot_notify(7, 9);
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 4, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 3\n", ret);
    check_hot_notify();
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == -1, "Hot item: %lx, expected -1\n", ret);
    /* enabling the button won't change that */
    SendMessage(hToolbar, TB_ENABLEBUTTON, 9, TRUE);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == -1, "TB_SETHOTITEM returned %ld, expected -1\n", ret);

    /* disabling a hot button works */
    ret = SendMessage(hToolbar, TB_SETHOTITEM, 3, 0);
    ok(ret == -1, "TB_SETHOTITEM returned %ld, expected -1\n", ret);
    g_fReceivedHotItemChange = FALSE;
    SendMessage(hToolbar, TB_ENABLEBUTTON, 7, FALSE);
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 3, "TB_SETHOTITEM returned %ld, expected 3\n", ret);
    ok(g_fReceivedHotItemChange == FALSE, "Unexpected TBN_HOTITEMCHANGE\n");

    SendMessage(hToolbar, TB_SETHOTITEM, 1, 0);
    tbinfo.cbSize = sizeof(TBBUTTONINFO);
    tbinfo.dwMask = TBIF_STATE;
    tbinfo.fsState = 0;  /* disabled */
    g_fReceivedHotItemChange = FALSE;
    ok(SendMessage(hToolbar, TB_SETBUTTONINFO, 1, (LPARAM)&tbinfo) == TRUE, "TB_SETBUTTONINFO failed\n");
    ret = SendMessage(hToolbar, TB_GETHOTITEM, 0, 0);
    ok(ret == 1, "TB_SETHOTITEM returned %ld, expected 1\n", ret);
    ok(g_fReceivedHotItemChange == FALSE, "Unexpected TBN_HOTITEMCHANGE\n");

    DestroyWindow(hToolbar);
}

#if 0  /* use this to generate more tests*/

static void dump_sizes(HWND hToolbar)
{
    SIZE sz;
    RECT r;
    int count = SendMessage(hToolbar, TB_BUTTONCOUNT, 0, 0);
    int i;

    GetClientRect(hToolbar, &r);
    SendMessageA(hToolbar, TB_GETMAXSIZE, 0, &sz);
    printf("  { {%d, %d, %d, %d}, {%d, %d}, %d, {", r.left, r.top, r.right, r.bottom,
        sz.cx, sz.cy, count);
    for (i=0; i<count; i++)
    {
        SendMessageA(hToolbar, TB_GETITEMRECT, i, &r);
        printf("%s{%3d, %3d, %3d, %3d}, ", (i%3==0 ? "\n    " : ""), r.left, r.top, r.right, r.bottom);
    }
    printf("\n  }, },\n");
}

#define check_sizes() dump_sizes(hToolbar);
#define check_sizes_todo(todomask) dump_sizes(hToolbar);

#else

static int system_font_height(void) {
    HDC hDC;
    TEXTMETRIC tm;

    hDC = CreateCompatibleDC(NULL);
    GetTextMetrics(hDC, &tm);
    DeleteDC(NULL);

    return tm.tmHeight;
}

static int string_width(const CHAR *s) {
    SIZE sz;
    HDC hdc;

    hdc = CreateCompatibleDC(NULL);
    GetTextExtentPoint32A(hdc, s, strlen(s), &sz);
    DeleteDC(hdc);

    return sz.cx;
}

typedef struct
{
    RECT rcClient;
    SIZE szMin;
    INT nButtons;
    RECT *prcButtons;
} tbsize_result_t;

static tbsize_result_t init_tbsize_result(int nButtonsAlloc, int cleft, int ctop, int cright, int cbottom, int minx, int miny) {
    tbsize_result_t ret;

    SetRect(&ret.rcClient, cleft, ctop, cright, cbottom);
    ret.szMin.cx = minx;
    ret.szMin.cy = miny;
    ret.nButtons = 0;
    ret.prcButtons = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nButtonsAlloc*sizeof(RECT));

    return ret;
}

static void tbsize_addbutton(tbsize_result_t *tbsr, int left, int top, int right, int bottom) {
    SetRect(&tbsr->prcButtons[tbsr->nButtons], left, top, right, bottom);
    tbsr->nButtons++;
}

#define STRING0 "A"
#define STRING1 "MMMMMMMMMMMMM"
#define STRING2 "Tst"

static tbsize_result_t *tbsize_results;

#define tbsize_results_num 24

static void init_tbsize_results(void) {
    int fontheight = system_font_height();
    int buttonwidth;

    tbsize_results = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, tbsize_results_num*sizeof(tbsize_result_t));

    tbsize_results[0] = init_tbsize_result(5, 0, 0 ,672 ,26, 100 ,22);
    tbsize_addbutton(&tbsize_results[0],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[0],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[0],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[0],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[0],  77,   2, 100,  24);

    tbsize_results[1] = init_tbsize_result(7, 0, 0, 672, 26, 146, 22);
    tbsize_addbutton(&tbsize_results[1],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[1],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[1],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[1],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[1],  77,   2, 100,  24);
    tbsize_addbutton(&tbsize_results[1], 100,   2, 123,  24);
    tbsize_addbutton(&tbsize_results[1],   0,   24, 23,  46);

    tbsize_results[2] = init_tbsize_result(7, 0, 0, 672, 26, 146, 22);
    tbsize_addbutton(&tbsize_results[2],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[2],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[2],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[2],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[2],  77,   2, 100,  24);
    tbsize_addbutton(&tbsize_results[2], 100,   2, 123,  24);
    tbsize_addbutton(&tbsize_results[2],   0,   24, 23,  46);

    tbsize_results[3] = init_tbsize_result(7, 0, 0, 672, 26, 146, 22);
    tbsize_addbutton(&tbsize_results[3],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[3],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[3],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[3],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[3],  77,   2, 100,  24);
    tbsize_addbutton(&tbsize_results[3], 100,   2, 123,  24);
    tbsize_addbutton(&tbsize_results[3], 123,   2, 146,  24);

    tbsize_results[4] = init_tbsize_result(9, 0, 0, 672, 26, 192, 22);
    tbsize_addbutton(&tbsize_results[4],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[4],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[4],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[4],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[4],  77,   2, 100,  24);
    tbsize_addbutton(&tbsize_results[4], 100,   2, 123,  24);
    tbsize_addbutton(&tbsize_results[4], 123,   2, 146,  24);
    tbsize_addbutton(&tbsize_results[4], 146,   2, 169,  24);
    tbsize_addbutton(&tbsize_results[4], 169,   2, 192,  24);

    tbsize_results[5] = init_tbsize_result(39, 0, 0, 672, 92, 882, 22);
    tbsize_addbutton(&tbsize_results[5],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[5],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[5],   0,   2,   8,  29);
    tbsize_addbutton(&tbsize_results[5],   0,  29,  23,  51);
    tbsize_addbutton(&tbsize_results[5],  23,  29,  46,  51);
    tbsize_addbutton(&tbsize_results[5],  46,  29,  69,  51);
    tbsize_addbutton(&tbsize_results[5],  69,  29,  92,  51);
    tbsize_addbutton(&tbsize_results[5],  92,  29, 115,  51);
    tbsize_addbutton(&tbsize_results[5], 115,  29, 138,  51);
    tbsize_addbutton(&tbsize_results[5], 138,  29, 161,  51);
    tbsize_addbutton(&tbsize_results[5], 161,  29, 184,  51);
    tbsize_addbutton(&tbsize_results[5], 184,  29, 207,  51);
    tbsize_addbutton(&tbsize_results[5], 207,  29, 230,  51);
    tbsize_addbutton(&tbsize_results[5], 230,  29, 253,  51);
    tbsize_addbutton(&tbsize_results[5], 253,  29, 276,  51);
    tbsize_addbutton(&tbsize_results[5], 276,  29, 299,  51);
    tbsize_addbutton(&tbsize_results[5], 299,  29, 322,  51);
    tbsize_addbutton(&tbsize_results[5], 322,  29, 345,  51);
    tbsize_addbutton(&tbsize_results[5], 345,  29, 368,  51);
    tbsize_addbutton(&tbsize_results[5], 368,  29, 391,  51);
    tbsize_addbutton(&tbsize_results[5], 391,  29, 414,  51);
    tbsize_addbutton(&tbsize_results[5], 414,  29, 437,  51);
    tbsize_addbutton(&tbsize_results[5], 437,  29, 460,  51);
    tbsize_addbutton(&tbsize_results[5], 460,  29, 483,  51);
    tbsize_addbutton(&tbsize_results[5], 483,  29, 506,  51);
    tbsize_addbutton(&tbsize_results[5], 506,  29, 529,  51);
    tbsize_addbutton(&tbsize_results[5], 529,  29, 552,  51);
    tbsize_addbutton(&tbsize_results[5], 552,  29, 575,  51);
    tbsize_addbutton(&tbsize_results[5], 575,  29, 598,  51);
    tbsize_addbutton(&tbsize_results[5], 598,  29, 621,  51);
    tbsize_addbutton(&tbsize_results[5], 621,  29, 644,  51);
    tbsize_addbutton(&tbsize_results[5], 644,  29, 667,  51);
    tbsize_addbutton(&tbsize_results[5],   0,  51,  23,  73);
    tbsize_addbutton(&tbsize_results[5],  23,  51,  46,  73);
    tbsize_addbutton(&tbsize_results[5],  46,  51,  69,  73);
    tbsize_addbutton(&tbsize_results[5],  69,  51,  92,  73);
    tbsize_addbutton(&tbsize_results[5],  92,  51, 115,  73);
    tbsize_addbutton(&tbsize_results[5], 115,  51, 138,  73);
    tbsize_addbutton(&tbsize_results[5], 138,  51, 161,  73);

    tbsize_results[6] = init_tbsize_result(7, 0, 0, 48, 226, 23, 140);
    tbsize_addbutton(&tbsize_results[6],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[6],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[6],  46,   2,  94,  24);
    tbsize_addbutton(&tbsize_results[6],  94,   2, 117,  24);
    tbsize_addbutton(&tbsize_results[6], 117,   2, 140,  24);
    tbsize_addbutton(&tbsize_results[6], 140,   2, 163,  24);
    tbsize_addbutton(&tbsize_results[6],   0,  24,  23,  46);

    tbsize_results[7] = init_tbsize_result(7, 0, 0, 92, 226, 23, 140);
    tbsize_addbutton(&tbsize_results[7],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[7],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[7],   0,  24,  92,  32);
    tbsize_addbutton(&tbsize_results[7],   0,  32,  23,  54);
    tbsize_addbutton(&tbsize_results[7],  23,  32,  46,  54);
    tbsize_addbutton(&tbsize_results[7],  46,  32,  69,  54);
    tbsize_addbutton(&tbsize_results[7],  69,  32,  92,  54);

    tbsize_results[8] = init_tbsize_result(7, 0, 0, 672, 26, 194, 30);
    tbsize_addbutton(&tbsize_results[8],   0,   2,  31,  32);
    tbsize_addbutton(&tbsize_results[8],  31,   2,  62,  32);
    tbsize_addbutton(&tbsize_results[8],  62,   2,  70,  32);
    tbsize_addbutton(&tbsize_results[8],  70,   2, 101,  32);
    tbsize_addbutton(&tbsize_results[8], 101,   2, 132,  32);
    tbsize_addbutton(&tbsize_results[8], 132,   2, 163,  32);
    tbsize_addbutton(&tbsize_results[8],   0,  32,  31,  62);

    tbsize_results[9] = init_tbsize_result(7, 0, 0, 672, 64, 194, 30);
    tbsize_addbutton(&tbsize_results[9],   0,   2,  31,  32);
    tbsize_addbutton(&tbsize_results[9],  31,   2,  62,  32);
    tbsize_addbutton(&tbsize_results[9],  62,   2,  70,  32);
    tbsize_addbutton(&tbsize_results[9],  70,   2, 101,  32);
    tbsize_addbutton(&tbsize_results[9], 101,   2, 132,  32);
    tbsize_addbutton(&tbsize_results[9], 132,   2, 163,  32);
    tbsize_addbutton(&tbsize_results[9],   0,  32,  31,  62);

    tbsize_results[10] = init_tbsize_result(7, 0, 0, 672, 64, 194, 30);
    tbsize_addbutton(&tbsize_results[10],   0,   0,  31,  30);
    tbsize_addbutton(&tbsize_results[10],  31,   0,  62,  30);
    tbsize_addbutton(&tbsize_results[10],  62,   0,  70,  30);
    tbsize_addbutton(&tbsize_results[10],  70,   0, 101,  30);
    tbsize_addbutton(&tbsize_results[10], 101,   0, 132,  30);
    tbsize_addbutton(&tbsize_results[10], 132,   0, 163,  30);
    tbsize_addbutton(&tbsize_results[10],   0,  30,  31,  60);

    tbsize_results[11] = init_tbsize_result(7, 0, 0, 124, 226, 31, 188);
    tbsize_addbutton(&tbsize_results[11],   0,    0,  31,  30);
    tbsize_addbutton(&tbsize_results[11],  31,    0,  62,  30);
    tbsize_addbutton(&tbsize_results[11],   0,   30, 124,  38);
    tbsize_addbutton(&tbsize_results[11],   0,   38,  31,  68);
    tbsize_addbutton(&tbsize_results[11],  31,   38,  62,  68);
    tbsize_addbutton(&tbsize_results[11],  62,   38,  93,  68);
    tbsize_addbutton(&tbsize_results[11],  93,   38, 124,  68);

    tbsize_results[12] = init_tbsize_result(7, 0, 0, 672, 26, 146, 22);
    tbsize_addbutton(&tbsize_results[12],   0,   2,  23,  24);
    tbsize_addbutton(&tbsize_results[12],  23,   2,  46,  24);
    tbsize_addbutton(&tbsize_results[12],  46,   2,  54,  24);
    tbsize_addbutton(&tbsize_results[12],  54,   2,  77,  24);
    tbsize_addbutton(&tbsize_results[12],  77,   2, 100,  24);
    tbsize_addbutton(&tbsize_results[12], 100,   2, 123,  24);
    tbsize_addbutton(&tbsize_results[12], 123,   2, 146,  24);

    tbsize_results[13] = init_tbsize_result(7, 0, 0, 672, 26, 146, 100);
    tbsize_addbutton(&tbsize_results[13],   0,   0,  23, 100);
    tbsize_addbutton(&tbsize_results[13],  23,   0,  46, 100);
    tbsize_addbutton(&tbsize_results[13],  46,   0,  54, 100);
    tbsize_addbutton(&tbsize_results[13],  54,   0,  77, 100);
    tbsize_addbutton(&tbsize_results[13],  77,   0, 100, 100);
    tbsize_addbutton(&tbsize_results[13], 100,   0, 123, 100);
    tbsize_addbutton(&tbsize_results[13], 123,   0, 146, 100);

    tbsize_results[14] = init_tbsize_result(10, 0, 0, 672, 26, 146, 100);
    tbsize_addbutton(&tbsize_results[14],   0,   0,  23, 100);
    tbsize_addbutton(&tbsize_results[14],  23,   0,  46, 100);
    tbsize_addbutton(&tbsize_results[14],  46,   0,  54, 100);
    tbsize_addbutton(&tbsize_results[14],  54,   0,  77, 100);
    tbsize_addbutton(&tbsize_results[14],  77,   0, 100, 100);
    tbsize_addbutton(&tbsize_results[14], 100,   0, 123, 100);
    tbsize_addbutton(&tbsize_results[14], 123,   0, 146, 100);
    tbsize_addbutton(&tbsize_results[14], 146,   0, 169, 100);
    tbsize_addbutton(&tbsize_results[14], 169,   0, 192, 100);
    tbsize_addbutton(&tbsize_results[14], 192,   0, 215, 100);

    tbsize_results[15] = init_tbsize_result(11, 0, 0, 672, 26, 238, 39);
    tbsize_addbutton(&tbsize_results[15],   0,   0,  23,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15],  23,   0,  46,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15],  46,   0,  54,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15],  54,   0,  77,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15],  77,   0, 100,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 100,   0, 123,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 123,   0, 146,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 146,   0, 169,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 169,   0, 192,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 192,   0, 215,  23 + fontheight);
    tbsize_addbutton(&tbsize_results[15], 215,   0, 238,  23 + fontheight);

    tbsize_results[16] = init_tbsize_result(11, 0, 0, 672, 26, 239, 22);
    tbsize_addbutton(&tbsize_results[16],   0,   0,  23,  22);
    tbsize_addbutton(&tbsize_results[16],  23,   0,  46,  22);
    tbsize_addbutton(&tbsize_results[16],  46,   0,  54,  22);
    tbsize_addbutton(&tbsize_results[16],  54,   0,  77,  22);
    tbsize_addbutton(&tbsize_results[16],  77,   0, 100,  22);
    tbsize_addbutton(&tbsize_results[16], 100,   0, 123,  22);
    tbsize_addbutton(&tbsize_results[16], 123,   0, 146,  22);
    tbsize_addbutton(&tbsize_results[16], 146,   0, 169,  22);
    tbsize_addbutton(&tbsize_results[16], 169,   0, 192,  22);
    tbsize_addbutton(&tbsize_results[16], 192,   0, 215,  22);
    tbsize_addbutton(&tbsize_results[16], 215,   0, 238,  22);

    buttonwidth = 7 + string_width(STRING1);

    tbsize_results[17] = init_tbsize_result(3, 0, 0, 672, 26, 489, 39);
    tbsize_addbutton(&tbsize_results[17],   0,   2, buttonwidth,  25 + fontheight);
    tbsize_addbutton(&tbsize_results[17], buttonwidth,   2, 2*buttonwidth + 4,  25 + fontheight);
    tbsize_addbutton(&tbsize_results[17], 2*buttonwidth + 4,   2, 3*buttonwidth + 4,  25 + fontheight);

    tbsize_results[18] = init_tbsize_result(6, 0, 0, 672, 104, 978, 24);
    tbsize_addbutton(&tbsize_results[18],   0,   2, buttonwidth,  10 + fontheight);
    tbsize_addbutton(&tbsize_results[18], buttonwidth,   2, 2*buttonwidth,  10 + fontheight);
    tbsize_addbutton(&tbsize_results[18], 2*buttonwidth,   2, 3*buttonwidth,  10 + fontheight);
    tbsize_addbutton(&tbsize_results[18], 3*buttonwidth,   2, 4*buttonwidth,  10 + fontheight);
    tbsize_addbutton(&tbsize_results[18], 4*buttonwidth,   2, 5*buttonwidth + 4,  10 + fontheight);
    tbsize_addbutton(&tbsize_results[18], 5*buttonwidth + 4,   2, 5*buttonwidth + 4 + string_width(STRING2) + 11,  10 + fontheight);

    tbsize_results[19] = init_tbsize_result(6, 0, 0, 672, 28, 978, 38);
    tbsize_addbutton(&tbsize_results[19],   0,   0, buttonwidth,  22 + fontheight);
    tbsize_addbutton(&tbsize_results[19], buttonwidth,   0, 2*buttonwidth,  22 + fontheight);
    tbsize_addbutton(&tbsize_results[19], 2*buttonwidth,   0, 3*buttonwidth,  22 + fontheight);
    tbsize_addbutton(&tbsize_results[19], 3*buttonwidth,   0, 4*buttonwidth,  22 + fontheight);
    tbsize_addbutton(&tbsize_results[19], 4*buttonwidth,   0, 5*buttonwidth + 4,  22 + fontheight);
    tbsize_addbutton(&tbsize_results[19], 5*buttonwidth + 4,   0, 5*buttonwidth + 4 + string_width(STRING2) + 11,  22 + fontheight);

    tbsize_results[20] = init_tbsize_result(3, 0, 0, 672, 100, 239, 102);
    tbsize_addbutton(&tbsize_results[20],   0,   2, 100, 102);
    tbsize_addbutton(&tbsize_results[20], 100,   2, 139, 102);
    tbsize_addbutton(&tbsize_results[20], 139,   2, 239, 102);

    tbsize_results[21] = init_tbsize_result(3, 0, 0, 672, 42, 185, 40);
    tbsize_addbutton(&tbsize_results[21],   0,   2,  75,  40);
    tbsize_addbutton(&tbsize_results[21],  75,   2, 118,  40);
    tbsize_addbutton(&tbsize_results[21], 118,   2, 165 + string_width(STRING2),  40);

    tbsize_results[22] = init_tbsize_result(1, 0, 0, 672, 42, 67, 40);
    tbsize_addbutton(&tbsize_results[22],   0,   2,  47 + string_width(STRING2),  40);

    tbsize_results[23] = init_tbsize_result(2, 0, 0, 672, 42, 67, 41);
    tbsize_addbutton(&tbsize_results[23],   0,   2, 672,  25 + fontheight);
    tbsize_addbutton(&tbsize_results[23],   0,  25 + fontheight, 672,  48 + 2*fontheight);
}

static void free_tbsize_results(void) {
    int i;

    for (i = 0; i < tbsize_results_num; i++)
        HeapFree(GetProcessHeap(), 0, tbsize_results[i].prcButtons);
    HeapFree(GetProcessHeap(), 0, tbsize_results);
    tbsize_results = NULL;
}

static int tbsize_numtests = 0;

typedef struct
{
    int test_num;
    int rect_index;
    RECT rcButton;
} tbsize_alt_result_t;

static tbsize_alt_result_t tbsize_alt_results[] =
{
  { 5, 2, { 0, 24, 8, 29 } },
  { 20, 1, { 100, 2, 107, 102 } },
  { 20, 2, { 107, 2, 207, 102 } }
};

static DWORD tbsize_alt_numtests = 0;

#define check_sizes_todo(todomask) { \
        RECT rc; \
        int buttonCount, i, mask=(todomask); \
        tbsize_result_t *res = &tbsize_results[tbsize_numtests]; \
        GetClientRect(hToolbar, &rc); \
        /*check_rect("client", rc, res->rcClient);*/ \
        buttonCount = SendMessage(hToolbar, TB_BUTTONCOUNT, 0, 0); \
        compare(buttonCount, res->nButtons, "%d"); \
        for (i=0; i<min(buttonCount, res->nButtons); i++) { \
            ok(SendMessageA(hToolbar, TB_GETITEMRECT, i, (LPARAM)&rc) == 1, "TB_GETITEMRECT\n"); \
            if (broken(tbsize_alt_numtests < sizeof(tbsize_alt_results)/sizeof(tbsize_alt_results[0]) && \
                       memcmp(&rc, &tbsize_alt_results[tbsize_alt_numtests].rcButton, sizeof(RECT)) == 0)) { \
                win_skip("Alternate rect found\n"); \
                tbsize_alt_numtests++; \
            } else if (!(mask&1)) { \
                check_rect("button = %d, tbsize_numtests = %d", rc, res->prcButtons[i], i, tbsize_numtests); \
            } else {\
                todo_wine { check_rect("button = %d, tbsize_numtests = %d", rc, res->prcButtons[i], i, tbsize_numtests); } \
            } \
            mask >>= 1; \
        } \
        tbsize_numtests++; \
    }

#define check_sizes() check_sizes_todo(0)

#endif

static TBBUTTON buttons1[] = {
    {0, 10, TBSTATE_WRAP|TBSTATE_ENABLED, 0, {0, }, 0, -1},
    {0, 11, 0, 0, {0, }, 0, -1},
};
static TBBUTTON buttons2[] = {
    {0, 20, TBSTATE_ENABLED, 0, {0, }, 0, -1},
    {0, 21, TBSTATE_ENABLED, 0, {0, }, 0, -1},
};
static TBBUTTON buttons3[] = {
    {0, 30, TBSTATE_ENABLED, 0, {0, }, 0, 0},
    {0, 31, TBSTATE_ENABLED, 0, {0, }, 0, 1},
    {0, 32, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0, }, 0, 1},
    {0, 33, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0, }, 0, (UINT_PTR)STRING2}
};

static void test_sizes(void)
{
    HWND hToolbar = NULL;
    HIMAGELIST himl, himl2;
    TBBUTTONINFO tbinfo;
    int style;
    int i;
    int fontheight = system_font_height();

    init_tbsize_results();

    rebuild_toolbar_with_buttons(&hToolbar);
    style = GetWindowLong(hToolbar, GWL_STYLE);
    ok(style == (WS_CHILD|WS_VISIBLE|CCS_TOP), "Invalid style %x\n", style);
    check_sizes();
    /* the TBSTATE_WRAP makes a second row */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons1);
    check_sizes();
    SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0);
    check_sizes();
    /* after setting the TBSTYLE_WRAPABLE the TBSTATE_WRAP is ignored */
    SetWindowLong(hToolbar, GWL_STYLE, style|TBSTYLE_WRAPABLE);
    check_sizes();
    /* adding new buttons with TBSTYLE_WRAPABLE doesn't add a new row */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons1);
    check_sizes();
    /* only after adding enough buttons the bar will be wrapped on a
     * separator and then on the first button */
    for (i=0; i<15; i++)
        SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons1);
    check_sizes_todo(0x4);

    rebuild_toolbar_with_buttons(&hToolbar);
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons1);
    /* setting the buttons vertical will only change the window client size */
    SetWindowLong(hToolbar, GWL_STYLE, style | CCS_VERT);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);
    check_sizes_todo(0x3c);
    /* with a TBSTYLE_WRAPABLE a wrapping will occur on the separator */
    SetWindowLong(hToolbar, GWL_STYLE, style | TBSTYLE_WRAPABLE | CCS_VERT);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);
    check_sizes_todo(0x7c);

    rebuild_toolbar_with_buttons(&hToolbar);
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons1);
    /* a TB_SETBITMAPSIZE changes button sizes*/
    SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(24, 24));
    check_sizes();

    /* setting a TBSTYLE_FLAT doesn't change anything - even after a TB_AUTOSIZE */
    SetWindowLong(hToolbar, GWL_STYLE, style | TBSTYLE_FLAT);
    SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0);
    check_sizes();
    /* but after a TB_SETBITMAPSIZE the top margins is changed */
    SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(20, 20));
    SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(24, 24));
    check_sizes();
    /* some vertical toolbar sizes */
    SetWindowLong(hToolbar, GWL_STYLE, style | TBSTYLE_FLAT | TBSTYLE_WRAPABLE | CCS_VERT);
    check_sizes_todo(0x7c);

    rebuild_toolbar_with_buttons(&hToolbar);
    SetWindowLong(hToolbar, GWL_STYLE, style | TBSTYLE_FLAT);
    /* newly added buttons will be use the previous margin */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons2);
    check_sizes();
    /* TB_SETBUTTONSIZE can't be used to reduce the size of a button below the default */
    check_button_size(hToolbar, 23, 22);
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(22, 21))==1, "TB_SETBUTTONSIZE\n");
    check_button_size(hToolbar, 23, 22);
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(5, 100))==1, "TB_SETBUTTONSIZE\n");
    check_button_size(hToolbar, 23, 100);
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(3, 3))==1, "TB_SETBUTTONSIZE\n");
    check_button_size(hToolbar, 23, 22);
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(5, 100))==1, "TB_SETBUTTONSIZE\n");
    check_button_size(hToolbar, 23, 100);
    check_sizes();
    /* add some buttons with non-default sizes */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons2);
    SendMessageA(hToolbar, TB_INSERTBUTTON, -1, (LPARAM)&buttons2[0]);
    check_sizes();
    SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[0]);
    /* TB_ADDSTRING resets the size */
    SendMessageA(hToolbar, TB_ADDSTRING, 0, (LPARAM) STRING0 "\0" STRING1 "\0");
    check_button_size(hToolbar, 23, 23 + fontheight);
    check_sizes();
    /* TB_SETBUTTONSIZE can be used to crop the text */
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(3, 3));
    check_button_size(hToolbar, 23, 22);
    check_sizes();
    /* the default size is bitmap size + padding */
    SendMessageA(hToolbar, TB_SETPADDING, 0, MAKELONG(1, 1));
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(3, 3));
    check_button_size(hToolbar, 17, 17);
    SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(3, 3));
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(3, 3));
    check_button_size(hToolbar, 4, 4);

    rebuild_toolbar(&hToolbar);
    /* sending a TB_SETBITMAPSIZE with the same sizes is enough to make the button smaller */
    check_button_size(hToolbar, 23, 22);
    SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(16, 15));
    check_button_size(hToolbar, 23, 21);
    /* -1 in TB_SETBITMAPSIZE is a special code meaning that the coordinate shouldn't be changed */
    add_128x15_bitmap(hToolbar, 16);
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(14, -1)), "TB_SETBITMAPSIZE failed\n");
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(21, 21), "%x");
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(-1, 12)), "TB_SETBITMAPSIZE failed\n");
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(21, 18), "%x");
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(-1, -1)), "TB_SETBITMAPSIZE failed\n");
    compare((int)SendMessageA(hToolbar, TB_GETBUTTONSIZE, 0, 0), MAKELONG(21, 18), "%x");
    /* check the imagelist */
    InvalidateRect(hToolbar, NULL, TRUE);
    UpdateWindow(hToolbar);
    CHECK_IMAGELIST(16, 14, 12);

    rebuild_toolbar(&hToolbar);
    SendMessageA(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)STRING0 "\0" STRING1 "\0");
    /* the height is increased after a TB_ADDSTRING */
    check_button_size(hToolbar, 23, 23 + fontheight);
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(100, 100));
    /* if a string is in the pool, even adding a button without a string resets the size */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons2[0]);
    check_button_size(hToolbar, 23, 22);
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(100, 100));
    /* an BTNS_AUTOSIZE button is also considered when computing the new size */
    SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[2]);
    check_button_size(hToolbar, 7 + string_width(STRING1), 23 + fontheight);
    SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[0]);
    check_sizes();
    /* delete button doesn't change the buttons size */
    SendMessageA(hToolbar, TB_DELETEBUTTON, 2, 0);
    SendMessageA(hToolbar, TB_DELETEBUTTON, 1, 0);
    check_button_size(hToolbar, 7 + string_width(STRING1), 23 + fontheight);
    /* TB_INSERTBUTTONS will */
    SendMessageA(hToolbar, TB_INSERTBUTTON, 1, (LPARAM)&buttons2[0]);
    check_button_size(hToolbar, 23, 22);

    /* TB_HIDEBUTTON and TB_MOVEBUTTON doesn't force a recalc */
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(100, 100));
    ok(SendMessageA(hToolbar, TB_MOVEBUTTON, 0, 1), "TB_MOVEBUTTON failed\n");
    check_button_size(hToolbar, 100, 100);
    ok(SendMessageA(hToolbar, TB_HIDEBUTTON, 20, TRUE), "TB_HIDEBUTTON failed\n");
    check_button_size(hToolbar, 100, 100);
    /* however changing the hidden flag with TB_SETSTATE does */
    ok(SendMessageA(hToolbar, TB_SETSTATE, 20, TBSTATE_ENABLED|TBSTATE_HIDDEN), "TB_SETSTATE failed\n");
    check_button_size(hToolbar, 100, 100);
    ok(SendMessageA(hToolbar, TB_SETSTATE, 20, TBSTATE_ENABLED), "TB_SETSTATE failed\n");
    check_button_size(hToolbar, 23, 22);

    /* TB_SETIMAGELIST always changes the height but the width only if necessary */
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(100, 100));
    himl = ImageList_LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_80x15), 20, 2, CLR_NONE, IMAGE_BITMAP, LR_DEFAULTCOLOR);
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl) == 0, "TB_SETIMAGELIST failed\n");
    check_button_size(hToolbar, 100, 21);
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(100, 100));
    check_button_size(hToolbar, 100, 100);
    /* But there are no update when we change imagelist, and image sizes are the same */
    himl2 = ImageList_LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_128x15), 20, 2, CLR_NONE, IMAGE_BITMAP, LR_DEFAULTCOLOR);
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, (LRESULT)himl2) == (LRESULT)himl, "TB_SETIMAGELIST failed\n");
    check_button_size(hToolbar, 100, 100);
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(1, 1));
    check_button_size(hToolbar, 27, 21);
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, 0) == (LRESULT)himl2, "TB_SETIMAGELIST failed\n");
    check_button_size(hToolbar, 27, 7);
    SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(1, 1));
    check_button_size(hToolbar, 8, 7)
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl) == 0, "TB_SETIMAGELIST failed\n");
    check_button_size(hToolbar, 27, 21)
    /* the text is taken into account */
    SendMessageA(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)STRING0 "\0" STRING1 "\0");
    SendMessageA(hToolbar, TB_ADDBUTTONS, 4, (LPARAM)buttons3);
    check_button_size(hToolbar, 7 + string_width(STRING1), 22 + fontheight);
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, 0) == (LRESULT)himl, "TB_SETIMAGELIST failed\n");
    check_button_size(hToolbar, 7 + string_width(STRING1), 8 + fontheight);
    /* the style change also comes into effect */
    check_sizes();
    SetWindowLong(hToolbar, GWL_STYLE, GetWindowLong(hToolbar, GWL_STYLE) | TBSTYLE_FLAT);
    ok(SendMessageA(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl) == 0, "TB_SETIMAGELIST failed\n");
    check_sizes_todo(0x30);     /* some small problems with BTNS_AUTOSIZE button sizes */

    rebuild_toolbar(&hToolbar);
    ImageList_Destroy(himl);
    ImageList_Destroy(himl2);

    SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]);
    check_button_size(hToolbar, 7 + string_width(STRING2), 23 + fontheight);
    SendMessageA(hToolbar, TB_DELETEBUTTON, 0, 0);
    check_button_size(hToolbar, 7 + string_width(STRING2), 23 + fontheight);

    rebuild_toolbar(&hToolbar);

    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32)) == 1, "TB_SETBITMAPSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(100, 100)) == 1, "TB_SETBUTTONSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons2[0]) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[2]) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[0]) == 1, "TB_ADDBUTTONS failed\n");
    SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0 );
    check_sizes();

    rebuild_toolbar(&hToolbar);
    SetWindowLong(hToolbar, GWL_STYLE, TBSTYLE_LIST | GetWindowLong(hToolbar, GWL_STYLE));
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32)) == 1, "TB_SETBITMAPSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(100, 100)) == 1, "TB_SETBUTTONSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons2[0]) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[2]) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]) == 1, "TB_ADDBUTTONS failed\n");
    SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0 );
    check_sizes_todo(0xff);

    rebuild_toolbar(&hToolbar);
    SetWindowLong(hToolbar, GWL_STYLE, TBSTYLE_LIST | GetWindowLong(hToolbar, GWL_STYLE));
    ok(SendMessageA(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32)) == 1, "TB_SETBITMAPSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(100, 100)) == 1, "TB_SETBUTTONSIZE failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]) == 1, "TB_ADDBUTTONS failed\n");
    SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0 );
    check_sizes();

    rebuild_toolbar(&hToolbar);
    SetWindowLong(hToolbar, GWL_STYLE, TBSTYLE_WRAPABLE | GetWindowLong(hToolbar, GWL_STYLE));
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]) == 1, "TB_ADDBUTTONS failed\n");
    ok(SendMessageA(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]) == 1, "TB_ADDBUTTONS failed\n");
    tbinfo.cx = 672;
    tbinfo.cbSize = sizeof(TBBUTTONINFO);
    tbinfo.dwMask = TBIF_SIZE | TBIF_BYINDEX;
    if (SendMessageA(hToolbar, TB_SETBUTTONINFO, 0, (LPARAM)&tbinfo))
    {
        ok(SendMessageA(hToolbar, TB_SETBUTTONINFO, 1, (LPARAM)&tbinfo) != 0, "TB_SETBUTTONINFO failed\n");
        SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0);
        check_sizes();
    }
    else  /* TBIF_BYINDEX probably not supported, confirm that this was the reason for the failure */
    {
        tbinfo.dwMask = TBIF_SIZE;
        ok(SendMessageA(hToolbar, TB_SETBUTTONINFO, 33, (LPARAM)&tbinfo) != 0, "TB_SETBUTTONINFO failed\n");
    }

    free_tbsize_results();
    DestroyWindow(hToolbar);
}

/* Toolbar control has two ways of reacting to a change. We call them a
 * relayout and recalc. A recalc forces a recompute of values like button size
 * and top margin (the latter in comctl32 <v6), while a relayout uses the cached
 * values. This functions creates a flat toolbar with a top margin of a non-flat
 * toolbar. We will notice a recalc, as it will recompte the top margin and
 * change it to zero*/
static void prepare_recalc_test(HWND *phToolbar)
{
    RECT rect;
    rebuild_toolbar_with_buttons(phToolbar);
    SetWindowLong(*phToolbar, GWL_STYLE,
        GetWindowLong(*phToolbar, GWL_STYLE) | TBSTYLE_FLAT);
    SendMessage(*phToolbar, TB_GETITEMRECT, 1, (LPARAM)&rect);
    ok(rect.top == 2, "Test will make no sense because initial top is %d instead of 2\n",
        rect.top);
}

static BOOL did_recalc(HWND hToolbar)
{
    RECT rect;
    SendMessage(hToolbar, TB_GETITEMRECT, 1, (LPARAM)&rect);
    ok(rect.top == 2 || rect.top == 0, "Unexpected top margin %d in recalc test\n",
        rect.top);
    return (rect.top == 0);
}

/* call after a recalc did happen to return to an unstable state */
static void restore_recalc_state(HWND hToolbar)
{
    RECT rect;
    /* return to style with a 2px top margin */
    SetWindowLong(hToolbar, GWL_STYLE,
        GetWindowLong(hToolbar, GWL_STYLE) & ~TBSTYLE_FLAT);
    /* recalc */
    SendMessage(hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&buttons3[3]);
    /* top margin will be 0px if a recalc occurs */
    SetWindowLong(hToolbar, GWL_STYLE,
        GetWindowLong(hToolbar, GWL_STYLE) | TBSTYLE_FLAT);
    /* safety check */
    SendMessage(hToolbar, TB_GETITEMRECT, 1, (LPARAM)&rect);
    ok(rect.top == 2, "Test will make no sense because initial top is %d instead of 2\n",
        rect.top);
}

static void test_recalc(void)
{
    HWND hToolbar = NULL;
    TBBUTTONINFO bi;
    CHAR test[] = "Test";
    const int EX_STYLES_COUNT = 5;
    int i;
    BOOL recalc;

    /* Like TB_ADDBUTTONS tested in test_sized, inserting a button without text
     * results in a relayout, while adding one with text forces a recalc */
    prepare_recalc_test(&hToolbar);
    SendMessage(hToolbar, TB_INSERTBUTTON, 1, (LPARAM)&buttons3[0]);
    recalc = did_recalc(hToolbar);
    ok(!recalc, "Unexpected recalc - adding button without text\n");

    prepare_recalc_test(&hToolbar);
    SendMessage(hToolbar, TB_INSERTBUTTON, 1, (LPARAM)&buttons3[3]);
    recalc = did_recalc(hToolbar);
    ok(recalc, "Expected a recalc - adding button with text\n");

    /* TB_SETBUTTONINFO, even when adding a text, results only in a relayout */
    prepare_recalc_test(&hToolbar);
    bi.cbSize = sizeof(bi);
    bi.dwMask = TBIF_TEXT;
    bi.pszText = test;
    SendMessage(hToolbar, TB_SETBUTTONINFO, 1, (LPARAM)&bi);
    recalc = did_recalc(hToolbar);
    ok(!recalc, "Unexpected recalc - setting a button text\n");

    /* most extended styled doesn't force a recalc (testing all the bits gives
     * the same results, but prints some ERRs while testing) */
    for (i = 0; i < EX_STYLES_COUNT; i++)
    {
        if (i == 1 || i == 3)  /* an undoc style and TBSTYLE_EX_MIXEDBUTTONS */
            continue;
        prepare_recalc_test(&hToolbar);
        expect(0, (int)SendMessage(hToolbar, TB_GETEXTENDEDSTYLE, 0, 0));
        SendMessage(hToolbar, TB_SETEXTENDEDSTYLE, 0, (1 << i));
        recalc = did_recalc(hToolbar);
        ok(!recalc, "Unexpected recalc - setting bit %d\n", i);
        SendMessage(hToolbar, TB_SETEXTENDEDSTYLE, 0, 0);
        recalc = did_recalc(hToolbar);
        ok(!recalc, "Unexpected recalc - clearing bit %d\n", i);
        expect(0, (int)SendMessage(hToolbar, TB_GETEXTENDEDSTYLE, 0, 0));
    }

    /* TBSTYLE_EX_MIXEDBUTTONS does a recalc on change */
    prepare_recalc_test(&hToolbar);
    SendMessage(hToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    recalc = did_recalc(hToolbar);
    if (recalc)
    {
        ok(recalc, "Expected a recalc - setting TBSTYLE_EX_MIXEDBUTTONS\n");
        restore_recalc_state(hToolbar);
        SendMessage(hToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
        recalc = did_recalc(hToolbar);
        ok(!recalc, "Unexpected recalc - setting TBSTYLE_EX_MIXEDBUTTONS again\n");
        restore_recalc_state(hToolbar);
        SendMessage(hToolbar, TB_SETEXTENDEDSTYLE, 0, 0);
        recalc = did_recalc(hToolbar);
        ok(recalc, "Expected a recalc - clearing TBSTYLE_EX_MIXEDBUTTONS\n");
    }
    else win_skip( "No recalc on TBSTYLE_EX_MIXEDBUTTONS\n" );

    /* undocumented exstyle 0x2 seems to change the top margin, which
     * interferes with these tests */

    DestroyWindow(hToolbar);
}

static void test_getbuttoninfo(void)
{
    HWND hToolbar = NULL;
    int i;

    rebuild_toolbar_with_buttons(&hToolbar);
    for (i = 0; i < 128; i++)
    {
        TBBUTTONINFO tbi;
        int ret;

        tbi.cbSize = i;
        tbi.dwMask = TBIF_COMMAND;
        ret = (int)SendMessage(hToolbar, TB_GETBUTTONINFO, 1, (LPARAM)&tbi);
        if (i == sizeof(TBBUTTONINFO)) {
            compare(ret, 0, "%d");
        } else {
            compare(ret, -1, "%d");
        }
    }
    DestroyWindow(hToolbar);
}

static void test_createtoolbarex(void)
{
    HWND hToolbar;
    TBBUTTON btns[3];
    ZeroMemory(&btns, sizeof(btns));

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, 20, 20, 16, 16, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 20, 20);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0x1a001b, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, 4, 4, 16, 16, sizeof(TBBUTTON));
    CHECK_IMAGELIST(32, 4, 4);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0xa000b, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, 0, 8, 12, 12, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 12, 12);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0x120013, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, -1, 8, 12, 12, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 12, 8);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0xe0013, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, -1, 8, -1, 12, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 16, 8);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0xe0017, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, 0, 0, 12, -1, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 12, 16);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0x160013, "%x");
    DestroyWindow(hToolbar);

    hToolbar = CreateToolbarEx(hMainWnd, WS_VISIBLE, 1, 16, GetModuleHandle(NULL), IDB_BITMAP_128x15, btns,
        3, 0, 0, 0, 12, sizeof(TBBUTTON));
    CHECK_IMAGELIST(16, 16, 16);
    compare((int)SendMessage(hToolbar, TB_GETBUTTONSIZE, 0, 0), 0x160017, "%x");
    DestroyWindow(hToolbar);
}

static void test_dispinfo(void)
{
    HWND hToolbar = NULL;
    const TBBUTTON buttons_disp[] = {
        {-1, 20, TBSTATE_ENABLED, 0, {0, }, 0, -1},
        {0,  21, TBSTATE_ENABLED, 0, {0, }, 0, -1},
    };
    BOOL ret;

    rebuild_toolbar(&hToolbar);
    SendMessageA(hToolbar, TB_LOADIMAGES, IDB_HIST_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons_disp);
    g_dwExpectedDispInfoMask = TBNF_IMAGE;
    /* Some TBN_GETDISPINFO tests will be done in MyWnd_Notify function.
     * We will receive TBN_GETDISPINFOW even if the control is ANSI */
    compare((BOOL)SendMessageA(hToolbar, CCM_GETUNICODEFORMAT, 0, 0), 0, "%d");
    ShowWindow(hToolbar, SW_SHOW);
    UpdateWindow(hToolbar);

    ret = (BOOL)SendMessageA(hToolbar, CCM_SETUNICODEFORMAT, TRUE, 0);
    compare(ret, FALSE, "%d");
    compare(SendMessageA(hToolbar, CCM_GETUNICODEFORMAT, 0, 0), 1L, "%ld");
    InvalidateRect(hToolbar, NULL, FALSE);
    UpdateWindow(hToolbar);

    ret = (BOOL)SendMessageA(hToolbar, CCM_SETUNICODEFORMAT, FALSE, 0);
    compare(ret, TRUE, "%d");
    compare(SendMessageA(hToolbar, CCM_GETUNICODEFORMAT, 0, 0), 0L, "%ld");
    InvalidateRect(hToolbar, NULL, FALSE);
    UpdateWindow(hToolbar);

    DestroyWindow(hToolbar);
    g_dwExpectedDispInfoMask = 0;
}

typedef struct
{
    int  nRows;
    BOOL bLarger;
    int  expectedRows;
} tbrows_result_t;

static tbrows_result_t tbrows_results[] =
{
    {1, TRUE,  1}, /* 0: Simple case 9 in a row */
    {2, TRUE,  2}, /* 1: Another simple case 5 on one row, 4 on another*/
    {3, FALSE, 3}, /* 2: 3 lines - should be 3 lines of 3 buttons */
    {8, FALSE, 5}, /* 3: 8 lines - should be 5 lines of 2 buttons */
    {8, TRUE,  9}, /* 4: 8 lines but grow - should be 9 lines */
    {1, TRUE,  1}  /* 5: Back to simple case */
};

static void test_setrows(void)
{
    TBBUTTON buttons[9];
    HWND hToolbar;
    DWORD i;

    for (i=0; i<9; i++)
        MakeButton(buttons+i, 1000+i, TBSTYLE_FLAT | TBSTYLE_CHECKGROUP, 0);

    /* Test 1 - 9 buttons */
    hToolbar = CreateToolbarEx(hMainWnd,
        WS_VISIBLE | WS_CLIPCHILDREN | WS_CHILD | CCS_NORESIZE | CCS_NOPARENTALIGN
        | CCS_NOMOVEY | CCS_TOP,
        0,
        0, NULL, 0,
        buttons, sizeof(buttons)/sizeof(buttons[0]),
        20, 20, 0, 0, sizeof(TBBUTTON));
    ok(hToolbar != NULL, "Toolbar creation\n");
    ok(SendMessageA(hToolbar, TB_AUTOSIZE, 0, 0) == 0, "TB_AUTOSIZE failed\n");

    /* test setting rows to each of 1-10 with bLarger true and false */
    for (i=0; i<(sizeof(tbrows_results) / sizeof(tbrows_result_t)); i++) {
        RECT rc;
        int rows;

        memset(&rc, 0xCC, sizeof(rc));
        SendMessageA(hToolbar, TB_SETROWS,
                     MAKELONG(tbrows_results[i].nRows, tbrows_results[i].bLarger),
                     (LPARAM) &rc);

        rows = SendMessageA(hToolbar, TB_GETROWS, MAKELONG(0,0), MAKELONG(0,0));
        ok(rows == tbrows_results[i].expectedRows,
                   "[%d] Unexpected number of rows %d (expected %d)\n", i, rows,
                   tbrows_results[i].expectedRows);
    }

    DestroyWindow(hToolbar);
}

static void test_getstring(void)
{
    HWND hToolbar = NULL;
    char str[10];
    WCHAR strW[10];
    static const char answer[] = "STR";
    static const WCHAR answerW[] = { 'S','T','R',0 };
    INT r;

    hToolbar = CreateWindowExA(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hMainWnd, (HMENU)5, GetModuleHandle(NULL), NULL);
    ok(hToolbar != NULL, "Toolbar creation problem\n");

    r = SendMessage(hToolbar, TB_GETSTRING, MAKEWPARAM(0, 0), 0);
    if (r == 0)
    {
        win_skip("TB_GETSTRING and TB_GETSTRINGW need 5.80\n");
        DestroyWindow(hToolbar);
        return;
    }
    expect(-1, r);
    r = SendMessage(hToolbar, TB_GETSTRINGW, MAKEWPARAM(0, 0), 0);
    expect(-1, r);
    r = SendMessage(hToolbar, TB_ADDSTRING, 0, (LPARAM)answer);
    expect(0, r);
    r = SendMessage(hToolbar, TB_GETSTRING, MAKEWPARAM(0, 0), 0);
    expect(lstrlenA(answer), r);
    r = SendMessage(hToolbar, TB_GETSTRINGW, MAKEWPARAM(0, 0), 0);
    expect(lstrlenA(answer), r);
    r = SendMessage(hToolbar, TB_GETSTRING, MAKEWPARAM(sizeof(str), 0), (LPARAM)str);
    expect(lstrlenA(answer), r);
    expect(0, lstrcmp(answer, str));
    r = SendMessage(hToolbar, TB_GETSTRINGW, MAKEWPARAM(sizeof(strW), 0), (LPARAM)strW);
    expect(lstrlenA(answer), r);
    expect(0, lstrcmpW(answerW, strW));

    DestroyWindow(hToolbar);
}

static void test_tooltip(void)
{
    HWND hToolbar = NULL;
    const TBBUTTON buttons_disp[] = {
        {-1, 20, TBSTATE_ENABLED, 0, {0, }, 0, -1},
        {0,  21, TBSTATE_ENABLED, 0, {0, }, 0, -1},
    };
    NMTTDISPINFOW nmtti;
    HWND tooltip;

    rebuild_toolbar(&hToolbar);

    SendMessageA(hToolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons_disp);

    /* W used to get through toolbar code that assumes tooltip is always Unicode */
    memset(&nmtti, 0, sizeof(nmtti));
    nmtti.hdr.code = TTN_GETDISPINFOW;
    nmtti.hdr.idFrom = 20;

    SendMessageA(hToolbar, CCM_SETUNICODEFORMAT, FALSE, 0);

    flush_sequences(sequences, NUM_MSG_SEQUENCES);
    SendMessageA(hToolbar, WM_NOTIFY, 0, (LPARAM)&nmtti);
    ok_sequence(sequences, PARENT_SEQ_INDEX, ttgetdispinfo_parent_seq,
                "dispinfo from tooltip", TRUE);

    g_ResetDispTextPtr = TRUE;
    SendMessageA(hToolbar, WM_NOTIFY, 0, (LPARAM)&nmtti);
    g_ResetDispTextPtr = FALSE;

    DestroyWindow(hToolbar);

    /* TBSTYLE_TOOLTIPS */
    hToolbar = CreateWindowExA(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        hMainWnd, (HMENU)5, GetModuleHandle(NULL), NULL);
    tooltip = (HWND)SendMessageA(hToolbar, TB_GETTOOLTIPS, 0, 0);
    ok(tooltip == NULL, "got %p\n", tooltip);
    DestroyWindow(hToolbar);
}

static void test_get_set_style(void)
{
    TBBUTTON buttons[9];
    DWORD style, style2, ret;
    HWND hToolbar;
    int i;

    for (i=0; i<9; i++)
        MakeButton(buttons+i, 1000+i, TBSTYLE_CHECKGROUP, 0);
    MakeButton(buttons+3, 1003, TBSTYLE_SEP|TBSTYLE_GROUP, 0);
    MakeButton(buttons+6, 1006, TBSTYLE_SEP, 0);

    hToolbar = CreateToolbarEx(hMainWnd,
        WS_VISIBLE | WS_CLIPCHILDREN | CCS_TOP |
        WS_CHILD | TBSTYLE_LIST,
        100,
        0, NULL, 0,
        buttons, sizeof(buttons)/sizeof(buttons[0]),
        0, 0, 20, 16, sizeof(TBBUTTON));
    ok(hToolbar != NULL, "Toolbar creation\n");
    SendMessage(hToolbar, TB_ADDSTRINGA, 0, (LPARAM)"test\000");

    style = SendMessageA(hToolbar, TB_GETSTYLE, 0, 0);
    style2 = GetWindowLongA(hToolbar, GWL_STYLE);
todo_wine
    ok(style == style2, "got 0x%08x, expected 0x%08x\n", style, style2);

    /* try to alter common window bits */
    style2 |= WS_BORDER;
    ret = SendMessageA(hToolbar, TB_SETSTYLE, 0, style2);
    ok(ret == 0, "got %d\n", ret);
    style = SendMessageA(hToolbar, TB_GETSTYLE, 0, 0);
    style2 = GetWindowLongA(hToolbar, GWL_STYLE);
    ok((style != style2) && (style == (style2 | WS_BORDER)),
        "got 0x%08x, expected 0x%08x\n", style, style2);
    ok(style & WS_BORDER, "got 0x%08x\n", style);

    /* now styles are the same, alter window style */
    ret = SendMessageA(hToolbar, TB_SETSTYLE, 0, style2);
    ok(ret == 0, "got %d\n", ret);
    style2 |= WS_BORDER;
    SetWindowLongA(hToolbar, GWL_STYLE, style2);
    style = SendMessageA(hToolbar, TB_GETSTYLE, 0, 0);
    ok(style == style2, "got 0x%08x, expected 0x%08x\n", style, style2);

    DestroyWindow(hToolbar);
}

static HHOOK g_tbhook;
static HWND g_toolbar;

DEFINE_EXPECT(g_hook_create);
DEFINE_EXPECT(g_hook_WM_NCCREATE);
DEFINE_EXPECT(g_hook_WM_CREATE);

static LRESULT WINAPI toolbar_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldproc = (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    LRESULT ret;
    DWORD style;

    if (msg == WM_NCCREATE)
    {
        if (g_toolbar == hwnd)
        {
            CHECK_EXPECT2(g_hook_WM_NCCREATE);
            g_toolbar = hwnd;
            ret = CallWindowProcA(oldproc, hwnd, msg, wParam, lParam);

            /* control is already set up */
            style = SendMessageA(hwnd, TB_GETSTYLE, 0, 0);
            ok(style != 0, "got %x\n", style);

            style = GetWindowLongA(hwnd, GWL_STYLE);
            ok((style & TBSTYLE_TOOLTIPS) == 0, "got 0x%08x\n", style);
            SetWindowLongA(hwnd, GWL_STYLE, style|TBSTYLE_TOOLTIPS);
            style = GetWindowLongA(hwnd, GWL_STYLE);
            ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%08x\n", style);

            return ret;
        }
    }
    else if (msg == WM_CREATE)
    {
        CREATESTRUCTA *cs = (CREATESTRUCTA*)lParam;

        if (g_toolbar == hwnd)
        {
            CHECK_EXPECT2(g_hook_WM_CREATE);

            style = GetWindowLongA(hwnd, GWL_STYLE);
            ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%08x\n", style);

            /* test if toolbar-specific messages are already working before WM_CREATE */
            style = SendMessageA(hwnd, TB_GETSTYLE, 0, 0);
            ok(style != 0, "got %x\n", style);
            ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%x\n", style);
            ok((cs->style & TBSTYLE_TOOLTIPS) == 0, "0x%08x\n", cs->style);

            ret = CallWindowProcA(oldproc, hwnd, msg, wParam, lParam);

            style = GetWindowLongA(hwnd, GWL_STYLE);
            ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%08x\n", style);

            /* test if toolbar-specific messages are already working before WM_CREATE */
            style = SendMessageA(hwnd, TB_GETSTYLE, 0, 0);
            ok(style != 0, "got %x\n", style);
            ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%x\n", style);

            return ret;
        }
    }

    return CallWindowProcA(oldproc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK cbt_hook_proc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_CREATEWND)
    {
        HWND hwnd = (HWND)wParam;

        if (!g_toolbar)
        {
            WNDPROC oldproc;

            CHECK_EXPECT2(g_hook_create);
            g_toolbar = hwnd;
            /* subclass */
            oldproc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)toolbar_subclass_proc);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)oldproc);
        }
        return 0;
    }

    return CallNextHookEx(g_tbhook, code, wParam, lParam);
}

static void test_create(void)
{
    HWND hwnd, tooltip;
    DWORD style;

    g_tbhook = SetWindowsHookA(WH_CBT, cbt_hook_proc);

    SET_EXPECT(g_hook_create);
    SET_EXPECT(g_hook_WM_NCCREATE);
    SET_EXPECT(g_hook_WM_CREATE);

    hwnd = CreateWindowExA(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        hMainWnd, (HMENU)5, GetModuleHandle(NULL), NULL);

    CHECK_CALLED(g_hook_create);
    CHECK_CALLED(g_hook_WM_NCCREATE);
    CHECK_CALLED(g_hook_WM_CREATE);

    style = GetWindowLongA(hwnd, GWL_STYLE);
    ok((style & TBSTYLE_TOOLTIPS) == TBSTYLE_TOOLTIPS, "got 0x%08x\n", style);

    tooltip = (HWND)SendMessageA(hwnd, TB_GETTOOLTIPS, 0, 0);
    ok(tooltip != NULL, "got %p\n", tooltip);
    ok(GetParent(tooltip) == hMainWnd, "got %p, %p\n", hMainWnd, hwnd);

    DestroyWindow(hwnd);
    UnhookWindowsHook(WH_CBT, cbt_hook_proc);

    /* TBSTYLE_TRANSPARENT */
    hwnd = CreateWindowExA(0, TOOLBARCLASSNAME, NULL,
        WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|TBSTYLE_FLAT|TBSTYLE_TOOLTIPS|TBSTYLE_GROUP,
        0, 0, 0, 0, hMainWnd, (HMENU)5, GetModuleHandle(NULL), NULL);

    style = GetWindowLongA(hwnd, GWL_STYLE);
    ok((style & TBSTYLE_TRANSPARENT) == TBSTYLE_TRANSPARENT, "got 0x%08x\n", style);

    style = SendMessageA(hwnd, TB_GETSTYLE, 0, 0);
    ok((style & TBSTYLE_TRANSPARENT) == TBSTYLE_TRANSPARENT, "got 0x%08x\n", style);

    DestroyWindow(hwnd);
}

typedef struct {
    DWORD mask;
    DWORD style;
    DWORD style_set;
} extended_style_t;

static const extended_style_t extended_style_test[] = {
    {
      TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER,
      TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER,
      TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER
    },
    {
      TBSTYLE_EX_MIXEDBUTTONS, TBSTYLE_EX_MIXEDBUTTONS,
      TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS
    },

    { 0, TBSTYLE_EX_MIXEDBUTTONS, TBSTYLE_EX_MIXEDBUTTONS },
    { 0, 0, 0 },
    { 0, TBSTYLE_EX_DRAWDDARROWS, TBSTYLE_EX_DRAWDDARROWS },
    { 0, TBSTYLE_EX_HIDECLIPPEDBUTTONS, TBSTYLE_EX_HIDECLIPPEDBUTTONS },

    { 0, 0, 0 },
    { TBSTYLE_EX_HIDECLIPPEDBUTTONS, TBSTYLE_EX_MIXEDBUTTONS, 0 },
    { TBSTYLE_EX_MIXEDBUTTONS, TBSTYLE_EX_HIDECLIPPEDBUTTONS, 0 },
    { TBSTYLE_EX_DOUBLEBUFFER, TBSTYLE_EX_MIXEDBUTTONS, 0 },

    {
      TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS,
      TBSTYLE_EX_MIXEDBUTTONS, TBSTYLE_EX_MIXEDBUTTONS
    },
    {
      TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS,
      TBSTYLE_EX_DOUBLEBUFFER, TBSTYLE_EX_DOUBLEBUFFER
    }
};

static void test_TB_GET_SET_EXTENDEDSTYLE(void)
{
    DWORD style, oldstyle, oldstyle2;
    const extended_style_t *ptr;
    HWND hwnd = NULL;
    int i;

    rebuild_toolbar(&hwnd);

    SendMessageA(hwnd, TB_SETEXTENDEDSTYLE, TBSTYLE_EX_DOUBLEBUFFER, TBSTYLE_EX_MIXEDBUTTONS);
    style = SendMessageA(hwnd, TB_GETEXTENDEDSTYLE, 0, 0);
    if (style == TBSTYLE_EX_MIXEDBUTTONS)
    {
        win_skip("Some extended style bits are not supported\n");
        DestroyWindow(hwnd);
        return;
    }

    for (i = 0; i < sizeof(extended_style_test)/sizeof(extended_style_t); i++)
    {
        ptr = &extended_style_test[i];

        oldstyle2 = SendMessageA(hwnd, TB_GETEXTENDEDSTYLE, 0, 0);

        oldstyle = SendMessageA(hwnd, TB_SETEXTENDEDSTYLE, ptr->mask, ptr->style);
        ok(oldstyle == oldstyle2, "%d: got old style 0x%08x, expected 0x%08x\n", i, oldstyle, oldstyle2);
        style = SendMessageA(hwnd, TB_GETEXTENDEDSTYLE, 0, 0);
        ok(style == ptr->style_set, "%d: got style 0x%08x, expected 0x%08x\n", i, style, ptr->style_set);
    }

    DestroyWindow(hwnd);
}

START_TEST(toolbar)
{
    WNDCLASSA wc;
    MSG msg;
    RECT rc;

    init_msg_sequences(sequences, NUM_MSG_SEQUENCES);

    InitCommonControls();
  
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hIcon = NULL;
    wc.hCursor = LoadCursorA(NULL, IDC_IBEAM);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "Toolbar test parent";
    wc.lpfnWndProc = parent_wnd_proc;
    RegisterClassA(&wc);
    
    hMainWnd = CreateWindowExA(0, "Toolbar test parent", "Blah", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 680, 260, NULL, NULL, GetModuleHandleA(NULL), 0);
    GetClientRect(hMainWnd, &rc);
    ShowWindow(hMainWnd, SW_SHOW);

    basic_test();
    test_add_bitmap();
    test_add_string();
    test_hotitem();
    test_sizes();
    test_recalc();
    test_getbuttoninfo();
    test_createtoolbarex();
    test_dispinfo();
    test_setrows();
    test_getstring();
    test_tooltip();
    test_get_set_style();
    test_create();
    test_TB_GET_SET_EXTENDEDSTYLE();

    PostQuitMessage(0);
    while(GetMessageA(&msg,0,0,0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    DestroyWindow(hMainWnd);
}
