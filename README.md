# AutoDarkTheme

A lightweight Windows utility that switches between light and dark mode on a daily schedule. Built with **C++17 and the native Win32 API**, with a settings window and a moon icon in the system tray.

## Features

- Configurable light and dark start times, including overnight schedules.
- Changes both Windows and application theme preferences.
- Applies the appropriate theme immediately when scheduling is enabled.
- Manual theme buttons that pause automatic switching.
- Tray controls for opening settings, resuming scheduling, and quitting.
- Optional background startup when you sign in to Windows.
- Recalculates scheduling after sleep, clock changes, and relevant time-zone notifications.
- Restores the tray icon after Windows Explorer restarts.
- Saves settings per Windows user and prevents duplicate instances in the same session.

## Requirements

**To run:** Windows 10 version 1607 or later, or Windows 11. The x64 build described below targets Intel/AMD 64-bit PCs.

**To build:** Visual Studio or Visual Studio Build Tools with the **Desktop development with C++** workload, MSVC compiler, and Windows SDK.

The app uses Windows system libraries. The build below includes the C++ runtime in the executable; recipients do not need Visual Studio, Python, or third-party libraries installed.

## Quick start

1. Put `AutoDarkTheme.exe` in a permanent folder.
2. Double-click it to open the settings window.
3. Choose your light and dark start times.
4. Check **Enable automatic switching**.
5. Click **Save schedule**.
6. Optionally check **Start when I sign in to Windows**.
7. Click **Close** or the window's **X** to keep the app running in the tray.

> **Saving times alone does not enable automatic switching.** Scheduling and startup with Windows are separate options. Both are off on a fresh setup.

### Default settings

| Setting | Default |
| --- | --- |
| Light mode starts | 07:00 |
| Dark mode starts | 19:00 |
| Automatic switching | Off until enabled and saved |
| Start with Windows | Off until enabled |

Times use the PC's **local time** and a **24-hour clock**. Light and dark start times must differ.

With the defaults, light mode runs from 07:00 up to 19:00, and dark mode runs from 19:00 until the following 07:00. Enabling the schedule at 20:00 applies dark mode immediately.

Overnight light schedules also work: light at 19:00 and dark at 07:00 keeps light mode active overnight.

## Controls

| Control | Behaviour |
| --- | --- |
| Enable automatic switching + Save schedule | Saves the times and scheduling state, then applies the current scheduled theme |
| Use light now | Applies light mode and saves the schedule as paused |
| Use dark now | Applies dark mode and saves the schedule as paused |
| Start when I sign in to Windows | Immediately adds or removes the Windows sign-in startup entry |
| Close / X | Closes the settings window while the scheduler remains in the tray |
| Quit app / tray Quit | Stops the app and automatic switching for the current session |

To resume after a manual change, right-click the moon icon and select **Resume saved schedule**, or enable automatic switching in the window and save.

Quitting does not remove the startup entry. If startup is enabled, the app can launch again at the next sign-in.

## Build from source

Open **x64 Native Tools Command Prompt for VS** from the Windows Start menu.

Change to your project folder, replacing this example path:

```bat
cd /d "C:\path\to\AutoDarkTheme"
```

Compile:

```bat
cl /nologo /std:c++17 /O1 /MT /EHsc /W4 /utf-8 AutoDarkTheme.cpp /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /OUT:AutoDarkTheme.exe
```

Run:

```bat
AutoDarkTheme.exe
```

The executable is created in the current folder.

| Option | Purpose |
| --- | --- |
| `/std:c++17` | Uses C++17 |
| `/O1` | Optimizes for code size |
| `/MT` | Statically links the C++ runtime |
| `/EHsc` | Enables standard C++ exception handling |
| `/W4` | Enables a high warning level |
| `/utf-8` | Uses UTF-8 character encoding |
| `/SUBSYSTEM:WINDOWS` | Builds a GUI executable without a console window |
| `/OPT:REF /OPT:ICF` | Removes unused sections and folds identical eligible sections |

Quit any running instance through its tray menu before rebuilding.

### Background launch

```bat
AutoDarkTheme.exe --tray
```

This starts without opening settings during normal operation. The window may still appear when startup encounters a problem, such as an unavailable tray.

## How it works

The scheduler determines the theme from the current local time and updates these current-user preferences:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
```

| Registry value | Light | Dark |
| --- | --- | --- |
| `AppsUseLightTheme` | 1 | 0 |
| `SystemUsesLightTheme` | 1 | 0 |

After a change, the app broadcasts `WM_SETTINGCHANGE` with `ImmersiveColorSet` so supporting applications can refresh.

A Windows timer is set for the next theme boundary. Resume and time-change notifications trigger recalculation. The message loop waits for events instead of continuously checking the time.

The settings window is created when opened and destroyed when closed. A hidden background window remains to receive timer, tray, and Windows notifications.

### Settings and startup

The schedule is stored as a versioned binary value named `Settings` under:

```text
HKEY_CURRENT_USER\Software\AutoDarkTheme
```

When startup is enabled, a value named `AutoDarkTheme` is registered under:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

It contains the quoted executable path followed by `--tray`.

Put the executable in its permanent folder before enabling startup. If you move it, open it from the new location and enable startup again to register the new path.

## Sharing

Share **AutoDarkTheme.exe** built with the command above with users of compatible x64 Windows PCs. They do not need the source or compiler.

Settings do not transfer with the executable. Each user must configure their own schedule and startup preference. An unsigned executable may trigger a Windows reputation warning.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Saved times, but the theme does not switch | Check **Enable automatic switching**, then click **Save schedule** |
| Does not start after reboot | Enable **Start when I sign in to Windows**; check Windows Startup apps and confirm the executable has not moved |
| Status says “Schedule paused” | Resume the saved schedule from the tray; manual theme buttons pause scheduling |
| Cannot find the moon icon | Check the taskbar's hidden-icons menu |
| Opening the executable does not create another instance | The existing instance is reused; open its settings from the tray |
| Some apps stay light | Check those apps' own theme settings; some may need reopening |
| `cl` is not recognized | Use the **x64 Native Tools Command Prompt for VS** and ensure the C++ workload is installed |
| Cannot overwrite the executable during a build | Quit the running app through its tray menu |
| Registry access error | Read the error shown by the app; device policies may restrict personalization or startup changes |

## Limitations

- The settings window uses standard Windows controls; its own appearance is not explicitly switched to a custom dark theme.
- Theme changes affect applications that follow Windows preferences; the app cannot force every application into dark mode.
- Scheduling requires the process to remain running.
- The app does not wake a sleeping PC; it rechecks the schedule after resume.
- Sunrise/sunset scheduling and location detection are not implemented.
- Other CPU architectures need separate builds and compatibility testing.
- Low resource use is a design goal. Actual RAM, idle CPU usage, and executable size depend on the build and target PC; no performance benchmark is claimed here.

## Verification

The developer confirmed basic operation after enabling the required options. The source includes compile-time assertions for schedule boundaries and overnight behaviour.

Cross-device compatibility, reboot behaviour, and all daylight-saving-time transitions have not been comprehensively verified.

## Removal

1. Uncheck **Start when I sign in to Windows**.
2. Quit the app through its tray menu.
3. Delete the executable.

The saved schedule remains in the current-user settings key documented above. Removing the executable does not restore a previous Windows theme.

---

Created by **Aman Yadav**.

