#include "../Window.h"
#include "../Platform.h"
#include "../Input.h"
#include "../Event.h"
#include "../Graphics.h"
#include "../String_.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Errors.h"
#include "../ExtMath.h"
#include "../Logger.h"
#include "../Options.h"
#include "../VirtualKeyboard.h"
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

static int vid_handle;
static OrbisKernelEqueue vid_flipQueue;
static off_t directMemPhysAddr;
static void* vid_mem;

void Window_PreInit(void) {
	Platform_LogConst("initing 1..");
}

// https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/docs/MD/PS4%20Libraries/VideoOut.md
// https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/docs/MD/PS4%20Libraries/Libkernel.md

#define DIRECT_MEM_ALIGN (2 * 1024 * 1024)
#define DIRECT_MEM_TOTAL (32 * 1024 * 1024)
static void AllocVideoMemory(void) {
	int res = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), 
											DIRECT_MEM_TOTAL, DIRECT_MEM_ALIGN, ORBIS_KERNEL_WC_GARLIC, &directMemPhysAddr);
	if (res < 0) Process_Abort2(res, "direct mem alloc");
	
	res = sceKernelMapDirectMemory(&vid_mem, DIRECT_MEM_TOTAL, 
									ORBIS_KERNEL_PROT_GPU_RW | ORBIS_KERNEL_PROT_CPU_RW, 0, directMemPhysAddr, DIRECT_MEM_ALIGN);
	if (res < 0) Process_Abort2(res, "direct mem map");
}

static void AllocVideoLink(void) {
	vid_handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);

	int res = sceKernelCreateEqueue(&vid_flipQueue, "flip queue");
	if (res < 0) Process_Abort2(res, "flip queue");
		
	sceVideoOutAddFlipEvent(vid_flipQueue, vid_handle, 0);
}

#define NUM_DISPLAY_BUFFERS 2
static char* vid_fbs[NUM_DISPLAY_BUFFERS];

#define _ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB 0x80000000
#define _ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB 0x80002200

static void AllocFramebuffers(void) {
	OrbisVideoOutBufferAttribute attr;
	int w = DisplayInfo.Width, h = DisplayInfo.Height;
	int res;
	Platform_Log2("%i, %i", &DisplayInfo.Width, &DisplayInfo.Height);

	cc_uintptr addr = (cc_uintptr)vid_mem;
	vid_fbs[0] = (char*)addr; 
	vid_fbs[1] = (char*)(addr + 16 * 1024 * 1024);
	
	sceVideoOutSetBufferAttribute(&attr, _ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, 
								ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9, w, h, w);
	
	res = sceVideoOutRegisterBuffers(vid_handle, 0, (void **)vid_fbs, NUM_DISPLAY_BUFFERS, &attr);
	if (res < 0) Process_Abort2(res, "vid buffers");
}

void Window_Init(void) {
	AllocVideoLink();

	OrbisVideoOutResolutionStatus res;
	sceVideoOutGetResolutionStatus(vid_handle, &res);
      
	DisplayInfo.Width  = res.width;
	DisplayInfo.Height = res.height;
	DisplayInfo.ScaleX = 1;
	DisplayInfo.ScaleY = 1;
	
	Window_Main.Width    = res.width;
	Window_Main.Height   = res.height;
	Window_Main.Focused  = true;
	
	Window_Main.Exists   = true;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

	DisplayInfo.ContentOffsetX = Option_GetOffsetX(20);
	DisplayInfo.ContentOffsetY = Option_GetOffsetY(20);
	Window_Main.SoftKeyboard   = SOFT_KEYBOARD_VIRTUAL;

	AllocVideoMemory();
	AllocFramebuffers();
}

void Window_Free(void) { }

void Window_Create2D(int width, int height) { 
	Window_Main.Is3D = false;
}

void Window_Create3D(int width, int height) { 
	Window_Main.Is3D = true;
}

void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) { }
void Clipboard_GetText(cc_string* value) { } // TODO sceClipboardGetText
void Clipboard_SetText(const cc_string* value) { } // TODO sceClipboardSetText

int Window_GetWindowState(void) { return WINDOW_STATE_FULLSCREEN; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int Window_IsObscured(void)            { return 0; }

void Window_Show(void) { }
void Window_SetSize(int width, int height) { }

void Window_RequestClose(void) {
	Event_RaiseVoid(&WindowEvents.Closing);
}


/*########################################################################################################################*
*----------------------------------------------------Input processing-----------------------------------------------------*
*#########################################################################################################################*/
void Window_ProcessEvents(float delta) {
}

void Cursor_SetPosition(int x, int y) { } // Makes no sense for PS Vita

void Window_EnableRawMouse(void)  { Input.RawMode = true; }
void Window_UpdateRawMouse(void)  {  }
void Window_DisableRawMouse(void) { Input.RawMode = false; }


/*########################################################################################################################*
*-------------------------------------------------------Gamepads----------------------------------------------------------*
*#########################################################################################################################*/\
static int pad_handle;

void Gamepads_PreInit(void) {
	int res = scePadInit();
	if (res < 0) Process_Abort2(res, "initing pad");
}

void Gamepads_Init(void) { 
	OrbisUserServiceInitializeParams param;
	param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
	sceUserServiceInitialize(&param);

	int userId;
	int res = sceUserServiceGetInitialUser(&userId);
	if (res < 0) Process_Abort2(res, "getting user");

    pad_handle = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (pad_handle < 0) Process_Abort2(pad_handle, "pad open");
}

void Gamepads_Process(float delta) {
	OrbisPadData data;
	int res = scePadRead(pad_handle, &data, 1);
	if (res < 0) Process_Abort2(res, "polling pad");
}


/*########################################################################################################################*
*------------------------------------------------------Framebuffer--------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = Mem_Alloc(width * height, 4, "bitmap");
	bmp->width  = width;
	bmp->height = height;
}

static unsigned vid_frameID, vid_fbIndex;
void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	// TODO test
	// TODO sleep instead of poll
	Platform_LogConst("waiting..");
	//while (sceVideoOutIsFlipPending(vid_handle)) { }
	Platform_LogConst("drawing..");

	char* dst = vid_fbs[vid_fbIndex];
	Mem_Copy(dst, bmp->scan0, bmp->width * bmp->height * 4);

	sceVideoOutSubmitFlip(vid_handle, vid_fbIndex, ORBIS_VIDEO_OUT_FLIP_VSYNC, vid_frameID);

	vid_frameID++;
	vid_fbIndex = (vid_fbIndex + 1) % NUM_DISPLAY_BUFFERS;
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
}


/*########################################################################################################################*
*------------------------------------------------------Soft keyboard------------------------------------------------------*
*#########################################################################################################################*/
void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) {
	if (Input.Sources & INPUT_SOURCE_NORMAL) return;

	VirtualKeyboard_Open(args);
}

void OnscreenKeyboard_SetText(const cc_string* text) {
	VirtualKeyboard_SetText(text);
}

void OnscreenKeyboard_Close(void) {
	VirtualKeyboard_Close();
}


/*########################################################################################################################*
*-------------------------------------------------------Misc/Other--------------------------------------------------------*
*#########################################################################################################################*/
void Window_ShowDialog(const char* title, const char* msg) {
	/* TODO implement */
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}
