// Target: Unity 6.0 LTS. Copy to Assets/MinvexBenchmark.cs in a Built-in project.
// Build: Unity -batchmode -quit -projectPath <project> -executeMethod
// MinvexBenchmark.Build -benchmarkPlayer <absolute player path>
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
#if UNITY_EDITOR
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
#endif

public sealed class MinvexBenchmark : MonoBehaviour
{
    readonly List<double> intervals = new List<double>();
    readonly FrameTiming[] timing = new FrameTiming[1];
    int objects = 1000, frames = 240, warmup = 60, frame;
    string output = "unity-benchmark.json";
    long previous;
    double sceneBuildMs;

    static string Argument(string name, string fallback)
    {
        string[] args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length; ++i)
            if (args[i] == name) { if (i + 1 == args.Length) throw new ArgumentException(name); return args[i + 1]; }
        return fallback;
    }
    static int Count(string name, int fallback)
    {
        int value = int.Parse(Argument(name, fallback.ToString()), CultureInfo.InvariantCulture);
        if (value < 1 || value > 100000) throw new ArgumentException(name + " must be in [1,100000]");
        return value;
    }
    void Awake()
    {
        try
        {
            var start = Stopwatch.StartNew();
            objects = Count("--objects", objects); frames = Count("--frames", frames); warmup = Count("--warmup", warmup);
            output = Argument("--output", output);
            if (GraphicsSettings.currentRenderPipeline != null) throw new InvalidOperationException("Use the Built-in Render Pipeline.");
            QualitySettings.vSyncCount = 0; QualitySettings.antiAliasing = 0;
            QualitySettings.shadows = ShadowQuality.Disable; Application.targetFrameRate = -1;
            Screen.SetResolution(1280, 720, FullScreenMode.Windowed);
            RenderSettings.ambientMode = AmbientMode.Flat; RenderSettings.ambientLight = new Color(.25f, .25f, .25f);
            RenderSettings.ambientIntensity = 1; RenderSettings.skybox = null; RenderSettings.fog = false;
            Material material = Resources.Load<Material>("MinvexBenchmarkMaterial");
            if (material == null) throw new InvalidOperationException("Run MinvexBenchmark.Build to include the diffuse material.");
            material.enableInstancing = false;
            int side = (int)Math.Ceiling(Math.Pow(objects, 1.0 / 3.0)); float center = side - 1;
            for (int i = 0; i < objects; ++i)
            {
                GameObject cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
                Destroy(cube.GetComponent<Collider>());
                cube.transform.position = new Vector3((i % side) * 2 - center, ((i / side) % side) * 2 - center, (i / (side * side)) * 2 - center);
                var renderer = cube.GetComponent<MeshRenderer>(); renderer.sharedMaterial = material;
                renderer.shadowCastingMode = ShadowCastingMode.Off; renderer.receiveShadows = false;
            }
            Camera camera = new GameObject("Camera").AddComponent<Camera>();
            camera.transform.position = new Vector3(side * 2.5f, side * 2.0f, side * 2.5f); camera.transform.LookAt(Vector3.zero);
            camera.fieldOfView = 60; camera.nearClipPlane = .1f; camera.farClipPlane = 1000;
            camera.allowHDR = false; camera.allowMSAA = false; camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(.035f, .045f, .065f, 1);
            Light sun = new GameObject("Sun").AddComponent<Light>(); sun.type = LightType.Directional;
            sun.color = Color.white; sun.intensity = .75f; sun.shadows = LightShadows.None;
            sun.transform.rotation = Quaternion.LookRotation(new Vector3(-.4f, -.8f, -.6f));
            sceneBuildMs = start.Elapsed.TotalMilliseconds;
        }
        catch (Exception e) { UnityEngine.Debug.LogException(e); Application.Quit(1); enabled = false; }
    }
    static string Q(string value)
    {
        var s = new StringBuilder("\"");
        foreach (char c in value) { if (c == '"' || c == '\\') s.Append('\\').Append(c); else if (c < 32) s.Append("\\u").Append(((int)c).ToString("x4")); else s.Append(c); }
        return s.Append('"').ToString();
    }
    void Update()
    {
        try
        {
            long now = Stopwatch.GetTimestamp();
            if (frame > warmup) intervals.Add((now - previous) * 1000.0 / Stopwatch.Frequency);
            previous = now; FrameTimingManager.CaptureFrameTimings(); ++frame;
            if (intervals.Count < frames) return;
            if (Screen.width != 1280 || Screen.height != 720) throw new InvalidOperationException("Unexpected framebuffer extent.");
            intervals.Sort(); double sum = 0; foreach (double value in intervals) sum += value;
            string gpuMs = "null";
            if (FrameTimingManager.GetLatestTimings(1, timing) != 0 && timing[0].gpuFrameTime > 0) gpuMs = timing[0].gpuFrameTime.ToString("R", CultureInfo.InvariantCulture);
            string backend = SystemInfo.graphicsDeviceType == GraphicsDeviceType.Direct3D11 ? "d3d11" : SystemInfo.graphicsDeviceType == GraphicsDeviceType.Metal ? "metal" : SystemInfo.graphicsDeviceType.ToString();
            string json = "{\"format\":\"MinvexBenchmark\",\"version\":2,\"engine\":\"Unity\",\"engineVersion\":" + Q(Application.unityVersion)
                + ",\"scenario\":\"shared-cubes-v2-unbatched\",\"objects\":" + objects + ",\"width\":1280,\"height\":720,\"msaa\":1,\"vsync\":false,\"shadows\":false,\"warmupFrames\":" + warmup + ",\"measuredFrames\":" + frames
                + ",\"lod\":false,\"instancing\":false,\"frustumCulling\":true,\"postProcessing\":false,\"compression\":\"none\""
                + ",\"backend\":" + Q(backend) + ",\"gpu\":" + Q(SystemInfo.graphicsDeviceName)
                + ",\"sceneBuildMs\":" + sceneBuildMs.ToString("R", CultureInfo.InvariantCulture)
                + ",\"runtimeInitMs\":null,\"modelLoadMs\":null,\"cadenceMeanMs\":" + (sum / frames).ToString("R", CultureInfo.InvariantCulture)
                + ",\"cadenceP95Ms\":" + intervals[(int)Math.Ceiling(frames * .95) - 1].ToString("R", CultureInfo.InvariantCulture)
                + ",\"cpuSubmissionMeanMs\":null,\"gpuLatestMs\":" + gpuMs
                + ",\"processResidentBytes\":" + Process.GetCurrentProcess().WorkingSet64 + ",\"executableBytes\":null}";
            File.WriteAllText(output, json, new UTF8Encoding(false)); enabled = false; Application.Quit(0);
        }
        catch (Exception e) { UnityEngine.Debug.LogException(e); enabled = false; Application.Quit(1); }
    }
#if UNITY_EDITOR
    public static void Build()
    {
        if (GraphicsSettings.currentRenderPipeline != null) throw new InvalidOperationException("Use a Built-in Render Pipeline project.");
        Directory.CreateDirectory("Assets/Resources");
        Shader shader = Shader.Find("Legacy Shaders/Diffuse"); if (shader == null) throw new InvalidOperationException("Diffuse shader unavailable.");
        const string materialPath = "Assets/Resources/MinvexBenchmarkMaterial.mat";
        if (AssetDatabase.LoadAssetAtPath<Material>(materialPath) == null) AssetDatabase.CreateAsset(new Material(shader), materialPath);
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        new GameObject("Benchmark").AddComponent<MinvexBenchmark>();
        const string scene = "Assets/MinvexBenchmark.unity"; EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(), scene);
        PlayerSettings.colorSpace = ColorSpace.Linear; PlayerSettings.enableFrameTimingStats = true;
        BuildTarget target = Application.platform == RuntimePlatform.OSXEditor ? BuildTarget.StandaloneOSX : BuildTarget.StandaloneWindows64;
        PlayerSettings.SetUseDefaultGraphicsAPIs(target, false);
        PlayerSettings.SetGraphicsAPIs(target, new[] { target == BuildTarget.StandaloneOSX ? GraphicsDeviceType.Metal : GraphicsDeviceType.Direct3D11 });
        PlayerSettings.SetStaticBatchingForPlatform(target, false);
#if UNITY_6000_0
        PlayerSettings.SetDynamicBatchingForPlatform(target, false);
#endif
        BuildReport report = BuildPipeline.BuildPlayer(new BuildPlayerOptions { scenes = new[] { scene }, locationPathName = Argument("-benchmarkPlayer", "Benchmark/UnityBenchmark.exe"), target = target, options = BuildOptions.None });
        if (report.summary.result != BuildResult.Succeeded) throw new InvalidOperationException("Benchmark build failed.");
    }
#endif
}
