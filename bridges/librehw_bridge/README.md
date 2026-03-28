# librehw_bridge

Small local .NET bridge that loads `LibreHardwareMonitorLib` and streams normalized NDJSON to `host_agent` over a named pipe.

## Default Local Reference

This scaffold defaults to your local fork paths:

- project: `E:\SQ_HQ\Thermal_Control\Monitoring\LibreHardwareMonitor\LibreHardwareMonitor-SQ-Thermal\LibreHardwareMonitorLib\LibreHardwareMonitorLib.csproj`
- fallback DLL: `E:\SQ_HQ\Thermal_Control\Monitoring\LibreHardwareMonitor\LibreHardwareMonitor-SQ-Thermal\bin\Release\x64\net8.0\LibreHardwareMonitorLib.dll`

You can override either at build time:

```powershell
dotnet build bridges/librehw_bridge/librehw_bridge.csproj `
  -p:LibreHardwareMonitorProjectPath="E:\path\to\LibreHardwareMonitorLib.csproj"
```

or:

```powershell
dotnet build bridges/librehw_bridge/librehw_bridge.csproj `
  -p:LibreHardwareMonitorLibPath="E:\path\to\LibreHardwareMonitorLib.dll"
```

## Run

```powershell
dotnet run --project bridges/librehw_bridge -- --pipe-name ollama_host_agent_lhm --machine-id snd-host
```

## Notes

- The bridge stays read-only.
- The main native host remains responsible for persistence and policy.
- This path is preferred over the LHM web server because it preserves stable hardware and sensor identifiers directly from the library API.
