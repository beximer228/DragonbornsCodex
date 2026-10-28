#pragma once

struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11DeviceContext;

// Lets an external test script drive the plugin while the game runs.
// Only active with testBridge=1 in DragonbornsCodex.ini.
//
// The script writes command lines into <SKSE log dir>\CheatMenuBridge\in.txt
// ("<id> <verb> <args...>", one per line); the plugin takes the file, runs each
// command on the render thread and appends one JSON object per command to out.jsonl:
// {"id":"...","ok":true,"data":...}. See tools/skytest.py.
//
// Dev builds only (CODEX_TEST_BRIDGE): the public build has no bridge at all, just these empty stubs.
namespace TestBridge
{
#ifdef CODEX_TEST_BRIDGE
	// Render thread, every frame.
	void Poll();

	// True while a screenshot is pending, so the overlay keeps rendering.
	bool NeedsFrame();

	// After the overlay is drawn: saves a pending screenshot of the back buffer.
	void AfterRender(IDXGISwapChain* a_swapChain, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
#else
	inline void Poll() {}
	inline bool NeedsFrame() { return false; }
	inline void AfterRender(IDXGISwapChain*, ID3D11Device*, ID3D11DeviceContext*) {}
#endif
}
