#pragma once
#include "pch.h"

#undef GetClassName
#undef SendMessage

struct AppSystemInfo_t
{
	const char* m_pModuleName;
	const char* m_pInterfaceName;
};

enum InitReturnVal_t
{
	INIT_FAILED = 0,
	INIT_OK,

	INIT_LAST_VAL,
};

enum AppSystemTier_t
{
	APP_SYSTEM_TIER0 = 0,
	APP_SYSTEM_TIER1,
	APP_SYSTEM_TIER2,
	APP_SYSTEM_TIER3,

	APP_SYSTEM_TIER_OTHER,
};
class IAppSystem // 8 funcs
{
  public:
	// Here's where the app systems get to learn about each other
	virtual bool Connect(/*CreateInterfaceFn*/ void* factory) = 0;
	virtual void Disconnect() = 0;

	// Here's where systems can access other interfaces implemented by this
	// object Returns NULL if it doesn't implement the requested interface
	virtual void* QueryInterface(const char* pInterfaceName) = 0;

	// Init, shutdown
	virtual InitReturnVal_t Init() = 0;
	virtual void Shutdown() = 0;

	// new:
	// Returns all dependent libraries
	virtual const AppSystemInfo_t* GetDependencies()
	{
		return NULL;
	}

	// Returns the tier
	virtual AppSystemTier_t GetTier()
	{
		return APP_SYSTEM_TIER_OTHER;
	}

	// Reconnect to a particular interface
	virtual void Reconnect(/*CreateInterfaceFn*/ void* factory, const char* pInterfaceName) {}
};
class IBaseInterface
{
  public:
	virtual ~IBaseInterface() {}
};

class KeyValues;
class IClientPanel;
class Vertex_t;
class IImage;
typedef unsigned char byte;
typedef void* PlaySoundFunc_t;
namespace vgui
{

	typedef void* VPANEL;
	typedef void* SurfacePlat; // maybe int?
	typedef void* Panel; // maybe int?
	typedef int HScheme;
	typedef int HFont;
	typedef int HTexture;
	typedef int ImageFormat; // ?

	class ISurface : public IAppSystem
	{
	  public:
		// call to Shutdown surface; surface can no longer be used after this is called
		virtual void Shutdown() = 0;

		// frame
		virtual void RunFrame() = 0; // 8

		// hierarchy root
		virtual VPANEL GetEmbeddedPanel() = 0; // 9
		virtual void SetEmbeddedPanel(VPANEL pPanel) = 0; // 10
		virtual void unk_11() = 0;
		// more...

		virtual void penis() = 0;
		virtual void unk_12() = 0;
		virtual void unk_13() = 0;
		virtual void unk_14() = 0;
		virtual void unk_15() = 0;
		virtual void unk_16() = 0;
		virtual void unk_17() = 0;
		virtual void unk_18() = 0;
		virtual void unk_19() = 0;
		virtual void unk_20() = 0;
		virtual void unk_21() = 0;
		virtual void unk_22() = 0;
		virtual void unk_23() = 0;
		virtual void unk_24() = 0;
		virtual void unk_25() = 0;
		virtual void unk_26() = 0;
		virtual void unk_27() = 0;
		virtual void unk_28() = 0;
		virtual void unk_29() = 0;
		virtual void unk_30() = 0;
		virtual void unk_31() = 0;
		virtual void unk_32() = 0;
		virtual void unk_33() = 0;
		virtual void unk_34() = 0;
		virtual void unk_35() = 0;
		virtual void unk_36() = 0;
		virtual void unk_37() = 0;
		virtual void unk_38() = 0;
		virtual void unk_39() = 0;
		virtual void unk_40() = 0;
		virtual void unk_41() = 0;
		virtual void unk_42() = 0;
		virtual void unk_43() = 0;
		virtual void unk_44() = 0;
		virtual void unk_45() = 0;
		virtual void unk_46() = 0;
		virtual void unk_47() = 0;
		virtual void unk_48() = 0;
		virtual void unk_49() = 0;
		virtual void unk_50() = 0;
		virtual void unk_51() = 0;
		virtual void unk_52() = 0;
		virtual void unk_53() = 0;
		virtual void unk_54() = 0;
		virtual void unk_55() = 0;
		virtual void unk_56() = 0;
		virtual void unk_57() = 0;
		virtual void unk_58() = 0;
		virtual void unk_59() = 0;
		virtual void unk_60() = 0;
		virtual void unk_61() = 0;
		virtual void unk_62() = 0;
		virtual void unk_63() = 0;
		virtual void unk_64() = 0;
		virtual void unk_65() = 0;
		virtual void unk_66() = 0;
		virtual void unk_67() = 0;
		virtual void unk_68() = 0;
		virtual void unk_69() = 0;
		virtual void unk_70() = 0;
		virtual void unk_71() = 0;
		virtual void unk_72() = 0;
		virtual void unk_73() = 0;
		virtual void unk_74() = 0;
		virtual void unk_75() = 0;
		virtual void unk_76() = 0;
		virtual void unk_77() = 0;
		virtual void unk_78() = 0;
		virtual void unk_79() = 0;
		virtual void unk_80() = 0;
		virtual void AddCustomFontFile(const char* fontFileName) = 0;
		virtual int GetFontTall(HFont font) = 0; //+656
		virtual void unk_83() = 0;
		virtual void unk_84() = 0;
		virtual void unk_85() = 0;
		virtual void unk_86() = 0;
		virtual void unk_87() = 0;
		virtual void unk_88() = 0;
		virtual void GetNotifyPanel() = 0; // return NULL;
		virtual void SetNotifyIcon(VPANEL context, HTexture icon, VPANEL panelToReceiveMessages, const char* text) = 0; // nullsub
		virtual void PlaySound(const char* fileName) = 0; // (a1 + 65920) = m_PlaySoundFunc

		virtual int GetPopupCount() = 0;
		virtual VPANEL GetPopup(int index) = 0;

		virtual bool ShouldPaintChildPanel(VPANEL childPanel) = 0;
		virtual bool RecreateContext(VPANEL panel) = 0;
		virtual void AddPanel(VPANEL panel) = 0;
		virtual void ReleasePanel(VPANEL panel) = 0;
		virtual void MovePopupToFront(VPANEL panel) = 0;
		virtual void MovePopupToBack(VPANEL panel) = 0;

		virtual void SolveTraverse(VPANEL panel, bool forceApplySchemeSettings = false) = 0;
		virtual void PaintTraverse(VPANEL panel) = 0;

		virtual void EnableMouseCapture(VPANEL panel, bool state) = 0;

		// returns the size of the workspace
		virtual void GetWorkspaceBounds(int& x, int& y, int& wide, int& tall) = 0;

		// gets the base resolution used in proportional mode
		virtual void GetProportionalBase(int& width, int& height) = 0;

		virtual void CalculateMouseVisible() = 0;
		virtual bool NeedKBInput() = 0;

		virtual bool HasCursorPosFunctions() = 0;
		virtual void SurfaceGetCursorPos(int& x, int& y) = 0;
		virtual void SurfaceSetCursorPos(int x, int y) = 0;

		// SRC only functions!!!
		virtual void DrawTexturedLine(const Vertex_t& a, const Vertex_t& b) = 0;
		virtual void DrawOutlinedCircle(int x, int y, int radius, int segments) = 0;
		virtual void DrawTexturedPolyLine(const Vertex_t* p, int n) = 0; // (Note: this connects the first and last points).
		virtual void DrawTexturedSubRect(int x0, int y0, int x1, int y1, float texs0, float text0, float texs1, float text1) = 0;
		virtual void DrawTexturedPolygon(int n, Vertex_t* pVertice, bool bClipVertices = true) = 0;
		virtual const wchar_t* GetTitle(VPANEL panel) = 0;
		virtual bool IsCursorLocked(void) const = 0;
		virtual void SetWorkspaceInsets(int left, int top, int right, int bottom) = 0;

		virtual void unk_118() = 0;
		virtual void unk_119() = 0;
		virtual void unk_120() = 0;
		virtual void unk_121() = 0;
		virtual void unk_122() = 0;
		virtual void unk_123() = 0;
		virtual void unk_124() = 0;
		virtual void unk_125() = 0;
		virtual void unk_126() = 0;

		virtual void PaintTraverseEx(VPANEL panel, bool paintPopups = false) = 0;

		virtual float GetZPos() const = 0;

		// From the Xbox
		virtual void SetPanelForInput(VPANEL vpanel) = 0;
		virtual void DrawFilledRectFastFade(
			int x0, int y0, int x1, int y1, int fadeStartPt, int fadeEndPt, unsigned int alpha0, unsigned int alpha1, bool bHorizontal) = 0;
		virtual void DrawFilledRectFade(int x0, int y0, int x1, int y1, unsigned int alpha0, unsigned int alpha1, bool bHorizontal) = 0;
		virtual void DrawSetTextureRGBAEx(int id, const unsigned char* rgba, int wide, int tall, ImageFormat imageFormat) = 0;
		virtual void DrawSetTextScale(float sx, float sy) = 0;
		virtual bool SetBitmapFontGlyphSet(/* unk args */) = 0;
		// adds a bitmap font file
		virtual bool AddBitmapFontFile(const char* fontFileName) = 0;
		// sets a symbol for the bitmap font
		virtual void SetBitmapFontName(const char* pName, const char* pFontFilename) = 0;
		// gets the bitmap font filename
		virtual const char* GetBitmapFontName(const char* pName) = 0;
		virtual void ClearTemporaryFontCache(void) = 0;

		virtual IImage* GetIconImageForFullPath(char const* pFullPath) = 0;
		virtual void DrawUnicodeString(const wchar_t* pwString /*, FontDrawType_t drawType = FONT_DRAW_DEFAULT*/) = 0;
		virtual void PrecacheFontCharacters(HFont font, const wchar_t* pCharacters) = 0;

		virtual const char* GetFontName(HFont font) = 0;

		virtual bool ForceScreenSizeOverride(bool bState, int wide, int tall) = 0;
		virtual bool ForceScreenPosOffset(bool bState, int x, int y) = 0;
		virtual void OffsetAbsPos(int& x, int& y) = 0;

		virtual void unk_146() = 0;
		virtual void unk_147() = 0;

		virtual void ResetFontCaches() = 0;

		virtual void unk_149() = 0; // maybe IsScreenSizeOverrideActive
		virtual void unk_150() = 0; // maybe IsScreenSizeOverrideActive
		virtual void unk_151() = 0;
		virtual void unk_152() = 0; // maybe DestroyTextureID

		virtual void DrawSetTextureFrame(int id, int nFrame, unsigned int* pFrameCache) = 0;
		virtual void GetFullscreenViewport(int& x, int& y, int& w, int& h) = 0;
		virtual void SetFullscreenViewport(int x, int y, int w, int h) = 0; // this uses NULL for the render target.

		virtual void SetSoftwareCursor(bool bUseSoftwareCursor) = 0;
		virtual bool IsPaintingSoftwareCursor() = 0;
		virtual void PaintSoftwareCursor() = 0;
	};

	class IPanel : public IBaseInterface
	{
	  public:
		virtual void Init(VPANEL vguiPanel, IClientPanel* panel) = 0;

		// methods
		virtual void SetPos(VPANEL vguiPanel, int x, int y) = 0;
		virtual void GetPos(VPANEL vguiPanel, int& x, int& y) = 0;
		virtual void SetSize(VPANEL vguiPanel, int wide, int tall) = 0;
		virtual void GetSize(VPANEL vguiPanel, int& wide, int& tall) = 0;
		virtual void SetSomeFloat(VPANEL vguiPanel, float unk) = 0; // new
		virtual void GetSomeFloat(VPANEL vguiPanel, float& unk) = 0; // new
		virtual void SetMinimumSize(VPANEL vguiPanel, int wide, int tall) = 0;
		virtual void GetMinimumSize(VPANEL vguiPanel, int& wide, int& tall) = 0;
		virtual void SetZPos(VPANEL vguiPanel, int z) = 0;
		virtual int GetZPos(VPANEL vguiPanel) = 0;

		virtual void GetAbsPos(VPANEL vguiPanel, int& x, int& y) = 0;
		virtual void SetClipRect(VPANEL vguiPanel, int x0, int y0, int x1, int y1) = 0; // new
		virtual void GetClipRect(VPANEL vguiPanel, int& x0, int& y0, int& x1, int& y1) = 0;
		virtual void SetInset(VPANEL vguiPanel, int left, int top, int right, int bottom) = 0;
		virtual void GetInset(VPANEL vguiPanel, int& left, int& top, int& right, int& bottom) = 0;

		virtual void SetVisible(VPANEL vguiPanel, bool state) = 0;
		virtual bool IsVisible(VPANEL vguiPanel) = 0;
		virtual void SetParent(VPANEL vguiPanel, VPANEL newParent) = 0;
		virtual int GetChildCount(VPANEL vguiPanel) = 0;
		virtual VPANEL GetChild(VPANEL vguiPanel, int index) = 0;
		virtual /*CUtlVector<VPANEL>&*/ void* GetChildren(VPANEL vguiPanel) = 0;
		virtual void unk_18002B900() = 0; // new
		virtual void unk_18002B910() = 0; // new
		virtual void unk_18002B920() = 0; // new //  I COMMENTED THIS HERE CUZ IDK IM JUST TRYING //btw this i actually have to comment some
										  // random thing above drawcoloredtext here for that to work
		virtual VPANEL GetParent(VPANEL vguiPanel) = 0;
		virtual void MoveToFront(VPANEL vguiPanel) = 0;
		virtual void MoveToBack(VPANEL vguiPanel) = 0;
		virtual bool HasParent(VPANEL vguiPanel, VPANEL potentialParent) = 0;
		virtual bool IsPopup(VPANEL vguiPanel) = 0;
		virtual void SetPopup(VPANEL vguiPanel, bool state) = 0;
		virtual bool IsFullyVisible(VPANEL vguiPanel) = 0;

		// gets the scheme this panel uses
		virtual HScheme GetScheme(VPANEL vguiPanel) = 0;
		// gets whether or not this panel should scale with screen resolution
		virtual bool IsProportional(VPANEL vguiPanel) = 0;
		// returns true if auto-deletion flag is set
		virtual bool IsAutoDeleteSet(VPANEL vguiPanel) = 0;
		// deletes the Panel * associated with the vpanel
		virtual void DeletePanel(VPANEL vguiPanel) = 0;

		// input interest
		virtual void SetKeyBoardInputEnabled(VPANEL vguiPanel, bool state) = 0;
		virtual void SetMouseInputEnabled(VPANEL vguiPanel, bool state) = 0;
		virtual bool IsKeyBoardInputEnabled(VPANEL vguiPanel) = 0;
		virtual bool IsMouseInputEnabled(VPANEL vguiPanel) = 0;

		// calculates the panels current position within the hierarchy
		virtual void Solve(VPANEL vguiPanel) = 0;

		// gets names of the object (for debugging purposes)
		virtual const char* GetName(VPANEL vguiPanel) = 0;
		virtual const char* GetClassName(VPANEL vguiPanel) = 0;

		// delivers a message to the panel
		virtual void SendMessage(VPANEL vguiPanel, KeyValues* params, VPANEL ifromPanel) = 0;

		// these pass through to the IClientPanel
		virtual void Think(VPANEL vguiPanel) = 0;
		// virtual void PerformApplySchemeSettings(VPANEL vguiPanel) = 0; // gone?
		virtual void PaintTraverse(VPANEL vguiPanel, bool forceRepaint, bool allowForce = true) = 0;
		virtual void Repaint(VPANEL vguiPanel) = 0;
		virtual VPANEL IsWithinTraverse(VPANEL vguiPanel, int x, int y, bool traversePopups) = 0;
		virtual void OnChildAdded(VPANEL vguiPanel, VPANEL child) = 0;
		virtual void OnSizeChanged(VPANEL vguiPanel, int newWide, int newTall) = 0;

		virtual void InternalFocusChanged(VPANEL vguiPanel, bool lost) = 0;
		virtual bool RequestInfo(VPANEL vguiPanel, KeyValues* outputData) = 0;
		virtual void RequestFocus(VPANEL vguiPanel, int direction = 0) = 0;
		virtual bool RequestFocusPrev(VPANEL vguiPanel, VPANEL existingPanel) = 0;
		virtual bool RequestFocusNext(VPANEL vguiPanel, VPANEL existingPanel) = 0;
		virtual VPANEL GetCurrentKeyFocus(VPANEL vguiPanel) = 0;
		virtual int GetTabPosition(VPANEL vguiPanel) = 0;

		// used by ISurface to store platform-specific data
		virtual SurfacePlat* Plat(VPANEL vguiPanel) = 0;
		virtual void SetPlat(VPANEL vguiPanel, SurfacePlat* Plat) = 0;

		// returns a pointer to the vgui controls baseclass Panel *
		// destinationModule needs to be passed in to verify that the returned Panel * is from the same module
		// it must be from the same module since Panel * vtbl may be different in each module
		virtual Panel* GetPanel(VPANEL vguiPanel, const char* destinationModule) = 0;

		virtual bool IsEnabled(VPANEL vguiPanel) = 0;
		virtual void SetEnabled(VPANEL vguiPanel, bool state) = 0;

		// Used by the drag/drop manager to always draw on top
		virtual bool IsTopmostPopup(VPANEL vguiPanel) = 0;
		virtual void SetTopmostPopup(VPANEL vguiPanel, bool state) = 0;

		// virtual void unk_18002BF30() = 0;
		virtual void unk_18002BF70() = 0;
		virtual void unk_18002BFA0() = 0;
		virtual void unk_18002BFD0() = 0;

		// sibling pins
		virtual void SetSiblingPin(VPANEL vguiPanel, VPANEL newSibling, byte iMyCornerToPin = 0, byte iSiblingCornerToPinTo = 0) = 0;

		virtual void unk_18002B250() = 0;
		virtual void unk_18002BE00() = 0;
	};

} // namespace vgui

// An extra interface implemented by the material system implementation of vgui::ISurface
class IMatSystemSurface : public vgui::ISurface
{
  public:
	virtual void unk_159() = 0;
	virtual void unk_160() = 0;
	virtual void unk_161() = 0;
	virtual void unk_162() = 0;
	virtual void unk_163() = 0;
	virtual void unk_164() = 0;
	virtual void unk_165() = 0;
	virtual void unk_166() = 0;
	virtual void unk_167() = 0;
	// virtual void unk_168() = 0;
	// virtual void unk_169() = 0;

	// Installs a function to play sounds
	virtual void InstallPlaySoundFunc(PlaySoundFunc_t soundFunc) = 0;

	// Some drawing methods that cannot be accomplished under Win32
	virtual void DrawColoredCircle(int centerx, int centery, float radius, int r, int g, int b, int a) = 0;
	virtual int DrawColoredText(vgui::HFont font, int x, int y, int r, int g, int b, int a, const char* fmt, ...) = 0;

	// Draws text with current font at position and wordwrapped to the rect using color values specified
	virtual void DrawColoredTextRect(vgui::HFont font, int x, int y, int w, int h, int r, int g, int b, int a, const char* fmt, ...) = 0;
	virtual void DrawTextHeight(vgui::HFont font, int w, int& h, const char* fmt, ...) = 0;

	// Returns the length of the text string in pixels
	virtual int DrawTextLen(vgui::HFont font, const char* fmt, ...) = 0;

	virtual void unk_176() = 0;
	virtual void unk_177() = 0;
	virtual void unk_178() = 0;
	virtual void unk_179() = 0;
	virtual void unk_180() = 0;
	virtual void unk_181() = 0;
	virtual void unk_182() = 0;
	virtual void unk_183() = 0;
	virtual void unk_184() = 0;
	virtual void unk_185() = 0;
	virtual void unk_186() = 0;
	virtual void unk_187() = 0;
};
