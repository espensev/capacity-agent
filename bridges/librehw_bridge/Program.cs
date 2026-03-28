using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using LibreHardwareMonitor.Hardware;

namespace LibreHwBridge;

internal static class Program
{
    private const int SchemaVersion = 1;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = false
    };

    private static int Main(string[] args)
    {
        BridgeOptions options = ParseOptions(args);
        using CancellationTokenSource cts = new();

        Console.CancelKeyPress += (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            cts.Cancel();
        };

        Computer computer = CreateComputer();
        UpdateVisitor visitor = new();

        try
        {
            computer.Open();
            DateTimeOffset startedAt = DateTimeOffset.UtcNow;

            while (!cts.IsCancellationRequested)
            {
                try
                {
                    computer.Accept(visitor);
                    List<CatalogSensor> catalog = BuildCatalog(computer);

                    using NamedPipeClientStream pipe = new(
                        ".",
                        options.PipeName,
                        PipeDirection.Out,
                        PipeOptions.Asynchronous);

                    pipe.Connect(5000);

                    using StreamWriter writer = new(
                        pipe,
                        new UTF8Encoding(encoderShouldEmitUTF8Identifier: false))
                    {
                        AutoFlush = true
                    };

                    Send(writer, BuildHello(options, catalog));
                    Send(writer, BuildCatalogSnapshot(options, catalog));

                    DateTimeOffset nextHeartbeat = DateTimeOffset.UtcNow.AddSeconds(30);

                    while (!cts.IsCancellationRequested && pipe.IsConnected)
                    {
                        computer.Accept(visitor);

                        DateTimeOffset now = DateTimeOffset.UtcNow;
                        Send(writer, BuildSampleBatch(options, computer, now));

                        if (now >= nextHeartbeat)
                        {
                            Send(writer, BuildHeartbeat(options, startedAt, catalog.Count));
                            nextHeartbeat = now.AddSeconds(30);
                        }

                        Thread.Sleep(options.SampleIntervalMs);
                    }
                }
                catch (Exception ex) when (!cts.IsCancellationRequested)
                {
                    Console.Error.WriteLine($"librehw_bridge reconnecting after error: {ex.Message}");
                    Thread.Sleep(options.ReconnectDelayMs);
                }
            }
        }
        finally
        {
            computer.Close();
        }

        return 0;
    }

    private static Computer CreateComputer()
    {
        return new Computer
        {
            IsCpuEnabled = true,
            IsGpuEnabled = true,
            IsMemoryEnabled = true,
            IsMotherboardEnabled = true,
            IsControllerEnabled = true,
            IsNetworkEnabled = true,
            IsStorageEnabled = true
        };
    }

    private static BridgeOptions ParseOptions(string[] args)
    {
        string pipeName = "ollama_host_agent_lhm";
        string machineId = Environment.MachineName.ToLowerInvariant();
        int sampleIntervalMs = 1000;
        int reconnectDelayMs = 2000;
        string source = "librehw_bridge";
        string bridgeVersion = "0.1.0";

        for (int i = 0; i < args.Length; i++)
        {
            string arg = args[i];
            string value = i + 1 < args.Length ? args[i + 1] : string.Empty;

            switch (arg)
            {
                case "--pipe-name":
                    pipeName = value;
                    i++;
                    break;
                case "--machine-id":
                    machineId = value;
                    i++;
                    break;
                case "--sample-ms":
                    sampleIntervalMs = int.Parse(value);
                    i++;
                    break;
                case "--reconnect-ms":
                    reconnectDelayMs = int.Parse(value);
                    i++;
                    break;
                case "--source":
                    source = value;
                    i++;
                    break;
                case "--bridge-version":
                    bridgeVersion = value;
                    i++;
                    break;
            }
        }

        return new BridgeOptions(
            PipeName: pipeName,
            MachineId: machineId,
            SampleIntervalMs: sampleIntervalMs,
            ReconnectDelayMs: reconnectDelayMs,
            Source: source,
            BridgeVersion: bridgeVersion);
    }

    private static HelloMessage BuildHello(BridgeOptions options, IReadOnlyList<CatalogSensor> catalog)
    {
        BridgeCapabilities capabilities = new(
            HardwareTypes: catalog.Select(c => c.HardwareType).Distinct().Order().ToArray(),
            SensorTypes: catalog.Select(c => c.SensorType).Distinct().Order().ToArray());

        return new HelloMessage(
            Type: "hello",
            SchemaVersion: SchemaVersion,
            Source: options.Source,
            MachineId: options.MachineId,
            SentAtUtc: UtcNow(),
            BridgeVersion: options.BridgeVersion,
            Hostname: Environment.MachineName,
            Pid: Environment.ProcessId,
            SampleIntervalMs: options.SampleIntervalMs,
            Capabilities: capabilities);
    }

    private static CatalogSnapshotMessage BuildCatalogSnapshot(BridgeOptions options, IReadOnlyList<CatalogSensor> catalog)
    {
        return new CatalogSnapshotMessage(
            Type: "catalog_snapshot",
            SchemaVersion: SchemaVersion,
            Source: options.Source,
            MachineId: options.MachineId,
            SentAtUtc: UtcNow(),
            Sensors: catalog);
    }

    private static SampleBatchMessage BuildSampleBatch(BridgeOptions options, Computer computer, DateTimeOffset sampleTime)
    {
        List<SensorSample> samples = new();

        foreach (IHardware hardware in computer.Hardware)
        {
            CollectSamplesRecursive(hardware, samples);
        }

        return new SampleBatchMessage(
            Type: "sample_batch",
            SchemaVersion: SchemaVersion,
            Source: options.Source,
            MachineId: options.MachineId,
            SentAtUtc: UtcNow(),
            SampleTimeUtc: sampleTime.ToUniversalTime().ToString("O"),
            Samples: samples);
    }

    private static HeartbeatMessage BuildHeartbeat(BridgeOptions options, DateTimeOffset startedAt, int catalogCount)
    {
        return new HeartbeatMessage(
            Type: "heartbeat",
            SchemaVersion: SchemaVersion,
            Source: options.Source,
            MachineId: options.MachineId,
            SentAtUtc: UtcNow(),
            Status: "ok",
            UptimeSeconds: (long)(DateTimeOffset.UtcNow - startedAt).TotalSeconds,
            CatalogCount: catalogCount);
    }

    private static List<CatalogSensor> BuildCatalog(Computer computer)
    {
        List<CatalogSensor> catalog = new();
        foreach (IHardware hardware in computer.Hardware)
        {
            CollectCatalogRecursive(hardware, catalog);
        }

        return catalog;
    }

    private static void CollectCatalogRecursive(IHardware hardware, List<CatalogSensor> catalog)
    {
        foreach (ISensor sensor in hardware.Sensors)
        {
            catalog.Add(new CatalogSensor(
                SensorUid: SensorUid(sensor),
                HardwareUid: HardwareUid(hardware),
                HardwarePath: hardware.Identifier.ToString(),
                HardwareName: hardware.Name,
                HardwareType: hardware.HardwareType.ToString(),
                SensorPath: sensor.Identifier.ToString(),
                SensorName: sensor.Name,
                SensorType: sensor.SensorType.ToString(),
                SensorIndex: sensor.Index,
                Unit: UnitFor(sensor.SensorType),
                IsDefaultHidden: sensor.IsDefaultHidden,
                Properties: new Dictionary<string, string>(hardware.Properties)));
        }

        foreach (IHardware subHardware in hardware.SubHardware)
        {
            CollectCatalogRecursive(subHardware, catalog);
        }
    }

    private static void CollectSamplesRecursive(IHardware hardware, List<SensorSample> samples)
    {
        foreach (ISensor sensor in hardware.Sensors)
        {
            if (!sensor.Value.HasValue)
            {
                continue;
            }

            samples.Add(new SensorSample(
                SensorUid: SensorUid(sensor),
                Value: sensor.Value.Value,
                Min: sensor.Min,
                Max: sensor.Max,
                Quality: "ok"));
        }

        foreach (IHardware subHardware in hardware.SubHardware)
        {
            CollectSamplesRecursive(subHardware, samples);
        }
    }

    private static string SensorUid(ISensor sensor)
    {
        return $"librehw:{sensor.Identifier}";
    }

    private static string HardwareUid(IHardware hardware)
    {
        return $"librehw:{hardware.Identifier}";
    }

    private static string UnitFor(SensorType sensorType)
    {
        return sensorType switch
        {
            SensorType.Voltage => "V",
            SensorType.Current => "A",
            SensorType.Power => "W",
            SensorType.Clock => "MHz",
            SensorType.Temperature => "C",
            SensorType.TemperatureRate => "C_per_s",
            SensorType.Load => "pct",
            SensorType.Frequency => "Hz",
            SensorType.Fan => "rpm",
            SensorType.Flow => "L_per_h",
            SensorType.Control => "pct",
            SensorType.Level => "pct",
            SensorType.Factor => "factor",
            SensorType.Data => "GiB",
            SensorType.SmallData => "MiB",
            SensorType.Throughput => "B_per_s",
            SensorType.TimeSpan => "s",
            SensorType.Timing => "ns",
            SensorType.Energy => "mWh",
            SensorType.Noise => "dBA",
            SensorType.Conductivity => "uS_per_cm",
            SensorType.Humidity => "pct",
            _ => "unknown"
        };
    }

    private static string UtcNow()
    {
        return DateTimeOffset.UtcNow.ToString("O");
    }

    private static void Send<T>(StreamWriter writer, T message)
    {
        string json = JsonSerializer.Serialize(message, JsonOptions);
        writer.WriteLine(json);
    }
}
