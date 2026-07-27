// PixelStrike3D — ImGui Bridge
// Package: com.pixellabs.pixelstrike3d
// Drop libinjection.so → Assets/Plugins/Android/arm64-v8a/
// Attach to a persistent GameObject.
// Script Execution Order → set to run LAST.

using System;
using System.Runtime.InteropServices;
using UnityEngine;

public class ImGuiUnityBridge : MonoBehaviour
{
    const string LIB = "injection";

    [DllImport(LIB)] static extern bool injection_init();
    [DllImport(LIB)] static extern void injection_shutdown();
    [DllImport(LIB)] static extern void injection_new_frame(float w, float h, float dt);
    [DllImport(LIB)] static extern void injection_render();
    [DllImport(LIB)] static extern void injection_set_touch(float x, float y, bool touching);
    [DllImport(LIB)] static extern void injection_set_shooting(bool shooting);
    [DllImport(LIB)] static extern bool injection_wants_mouse();
    [DllImport(LIB)] static extern bool injection_wants_keyboard();
    [DllImport(LIB)] static extern void injection_add_char(uint c);
    [DllImport(LIB)] static extern void injection_backspace();

    bool _initialized;
    bool _wasTouching;

    void Awake()
    {
        try {
            _initialized = injection_init();
            if (!_initialized) Debug.LogError("[ImGui] injection_init() failed — check logcat");
            else               Debug.Log("[ImGui] Ready");
        } catch (Exception e) {
            Debug.LogError($"[ImGui] {e.Message}\nEnsure libinjection.so is in Assets/Plugins/Android/arm64-v8a/");
        }
    }

    void OnDestroy() { if (_initialized) { injection_shutdown(); _initialized = false; } }

    void Update()
    {
        if (!_initialized) return;

        // Primary touch → ImGui mouse (Y flipped: Unity bottom-left → ImGui top-left)
        if (Input.touchCount > 0) {
            Touch t = Input.GetTouch(0);
            bool touching = t.phase != TouchPhase.Ended && t.phase != TouchPhase.Canceled;
            injection_set_touch(t.position.x, Screen.height - t.position.y, touching);
            _wasTouching = touching;
        } else if (_wasTouching) {
            injection_set_touch(0, 0, false);
            _wasTouching = false;
        }

        // Wire to your actual shoot button
        injection_set_shooting(Input.GetButton("Fire1"));

        injection_new_frame(Screen.width, Screen.height, Time.deltaTime);
    }

    void OnRenderObject() { if (_initialized) injection_render(); }

    public bool WantsMouse    => _initialized && injection_wants_mouse();
    public bool WantsKeyboard => _initialized && injection_wants_keyboard();
    public void SendChar(char c)  { if (_initialized) injection_add_char(c); }
    public void SendBackspace()   { if (_initialized) injection_backspace(); }
}
