// Fixed-operation, local-only profiling broker. Installed files and outputs must
// be administrator-owned. No request supplies an executable, arguments or path.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

internal static class Profiler {
    internal const string Root = @"C:\ProgramData\BruteForceProfiler";
    const string Instance = "BruteForceProfiler";
    static readonly string Exe = Path.Combine(Root, "Profiler.exe");
    static readonly string Wpr = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), @"System32\wpr.exe");
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    static readonly string[] Modes = { "cpu3", "cpu30", "gpu3", "gpu30", "combat30" };
    static readonly string[] Commands = { "status", "stop", "shutdown", "cpu3", "cpu30", "gpu3", "gpu30", "combat30" };
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process,uint flags,StringBuilder path,ref uint size);
    [StructLayout(LayoutKind.Sequential)] struct SecurityAttributes { public int Length; public IntPtr Descriptor; [MarshalAs(UnmanagedType.Bool)] public bool Inherit; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern Microsoft.Win32.SafeHandles.SafePipeHandle CreateNamedPipe(string name,uint openMode,uint pipeMode,uint maxInstances,uint outBuffer,uint inBuffer,uint timeout,ref SecurityAttributes security);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr security, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job, int kind, IntPtr value, uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)] struct Limits {
        public long ProcessTime, JobTime; public uint Flags;
        public UIntPtr MinWorking, MaxWorking; public uint ActiveLimit;
        public UIntPtr Affinity; public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct Io {
        public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
    }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
        public Limits Basic; public Io Io; public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    sealed class Job : IDisposable {
        IntPtr handle;
        internal Job() {
            handle=CreateJobObject(IntPtr.Zero,null);
            var limits=new ExtendedLimits(); limits.Basic.Flags=0x2000; // KILL_ON_JOB_CLOSE
            int size=Marshal.SizeOf(limits); IntPtr p=Marshal.AllocHGlobal(size);
            try { Marshal.StructureToPtr(limits,p,false);
                if(handle==IntPtr.Zero || !SetInformationJobObject(handle,9,p,(uint)size)) throw new IOException("Cannot create bounded process job");
            } finally { Marshal.FreeHGlobal(p); }
        }
        internal void Add(Process process) {
            if(!AssignProcessToJobObject(handle,process.Handle)) { try { process.Kill(); } catch {} throw new IOException("Cannot contain child process"); }
        }
        public void Dispose() { if(handle!=IntPtr.Zero) { CloseHandle(handle); handle=IntPtr.Zero; } }
    }
    sealed class Child : IDisposable {
        internal Process Process; Job job; StreamWriter log; readonly object gate=new object();
        internal Child(string file,string args,string logPath) {
            job=new Job(); log=new StreamWriter(new FileStream(logPath,FileMode.CreateNew,FileAccess.Write,FileShare.Read)); log.AutoFlush=true;
            Process=new Process(); Process.StartInfo=new ProcessStartInfo(file,args) {
                UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Root,
                RedirectStandardOutput=true,RedirectStandardError=true };
            // Do not inherit user-controlled executable/DLL search paths,
            // PowerShell/.NET injection variables, or writable temp/config roots.
            Process.StartInfo.EnvironmentVariables.Clear();
            string windows=Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            Process.StartInfo.EnvironmentVariables["SystemRoot"]=windows;
            Process.StartInfo.EnvironmentVariables["WINDIR"]=windows;
            Process.StartInfo.EnvironmentVariables["PATH"]=Path.Combine(windows,"System32");
            Process.StartInfo.EnvironmentVariables["COMSPEC"]=Path.Combine(windows,@"System32\cmd.exe");
            Process.StartInfo.EnvironmentVariables["TEMP"]=Path.Combine(Root,"temp");
            Process.StartInfo.EnvironmentVariables["TMP"]=Path.Combine(Root,"temp");
            Process.StartInfo.EnvironmentVariables["USERPROFILE"]=Path.Combine(Root,"profile");
            Process.StartInfo.EnvironmentVariables["LOCALAPPDATA"]=Path.Combine(Root,@"profile\Local");
            Process.StartInfo.EnvironmentVariables["APPDATA"]=Path.Combine(Root,@"profile\Roaming");
            Process.StartInfo.EnvironmentVariables["ProgramData"]=@"C:\ProgramData";
            Process.StartInfo.EnvironmentVariables["ProgramFiles"]=@"C:\Program Files";
            DataReceivedEventHandler receive=delegate(object sender,DataReceivedEventArgs e) { if(e.Data!=null) lock(gate) { if(log!=null) log.WriteLine(e.Data); } };
            Process.OutputDataReceived+=receive;Process.ErrorDataReceived+=receive;
            try { if(!Process.Start()) throw new IOException("Cannot start recorder"); job.Add(Process); Process.BeginOutputReadLine(); Process.BeginErrorReadLine(); }
            catch { Dispose();throw; }
        }
        internal int Wait(int milliseconds) {
            if(!Process.WaitForExit(milliseconds)) throw new TimeoutException("Recorder exceeded time limit");
            return Process.ExitCode;
        }
        public void Dispose() {
            if(job!=null) { job.Dispose();job=null; }
            if(Process!=null) { try { Process.WaitForExit(3000); } catch {} Process.Dispose();Process=null; }
            lock(gate) { if(log!=null) { log.Dispose();log=null; } }
        }
    }
    static string Utc() { return DateTime.UtcNow.ToString("o"); }
    static void Save(string file,object data) {
        string tmp=file+".tmp";File.WriteAllText(tmp,Json.Serialize(data),new UTF8Encoding(false));
        if(File.Exists(file)) File.Replace(tmp,file,null);else File.Move(tmp,file);
    }
    static Dictionary<string,object> Load(string file) { return Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(file)); }
    static Dictionary<string,object> Config() { return Load(Path.Combine(Root,"config.json")); }
    static string PipeName(string sid) { return "BruteForceProfiler-v1-"+sid; }
    static void RequireInstalledAdmin() {
        if(!String.Equals(Path.GetFullPath(Assembly.GetExecutingAssembly().Location),Exe,StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Run installed helper only");
        if(!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)) throw new UnauthorizedAccessException("Elevation required");
        if(WindowsIdentity.GetCurrent().Owner.Value!="S-1-5-32-544") throw new UnauthorizedAccessException("Administrator default object owner required");
    }
    internal static bool ValidCommand(string text) { return Array.IndexOf(Commands,text)>=0; }
    internal const long CaptureReserveBytes = 20L * 1024 * 1024 * 1024;
    internal static void RequireCaptureSpace(long available) {
        if(available<CaptureReserveBytes)
            throw new IOException("Capture refused: at least 20 GiB free on the protected recording volume is required. An administrator must archive/delete old recordings.");
    }
    static void CheckCaptureSpace() {
        RequireCaptureSpace(new DriveInfo(Path.GetPathRoot(Root)).AvailableFreeSpace);
    }
    static int RunWpr(string args,string directory,string logName,int timeout) {
        using(var child=new Child(Wpr,args+" -instancename "+Instance,Path.Combine(directory,logName))) return child.Wait(timeout);
    }
    static void Work(string mode,string id) {
        RequireInstalledAdmin();Guid guid;
        if(Array.IndexOf(Modes,mode)<0 || !Guid.TryParseExact(id,"N",out guid)) throw new ArgumentException("Invalid job");
        string dir=Path.Combine(Root,"recordings",id);
        if(!Directory.Exists(dir) || (File.GetAttributes(dir)&FileAttributes.ReparsePoint)!=0) throw new IOException("Invalid output directory");
        bool cpu=mode.StartsWith("cpu")||mode=="combat30",gpu=mode.StartsWith("gpu")||mode=="combat30";
        int duration=mode.EndsWith("3")?3:30;bool owned=false;Child nsys=null;
        var state=new Dictionary<string,object> { {"id",id},{"mode",mode},{"worker_pid",Process.GetCurrentProcess().Id},{"started_utc",Utc()},{"duration_seconds",duration},{"status","starting"} };
        string receipt=Path.Combine(dir,"result.json"),cancel=Path.Combine(dir,"cancel.flag");
        Save(receipt,state);
        try {
            if(File.Exists(cancel)) { state["status"]="cancelled";return; }
            CheckCaptureSpace(); // Repeat at actual start, before either recorder.
            if(cpu) {
                // Named instance keeps cleanup separate from unrelated WPR sessions.
                if(RunWpr("-start CPU -filemode",dir,"wpr-start.log",15000)!=0) throw new IOException("WPR start failed; see protected log");
                owned=true;
            }
            if(gpu) {
                string nsysPath=(string)Config()["nsys"];
                string args="profile --trace=wddm --wddm-additional-events=true --wddm-memory-trace=false --wddm-backtraces=false --sample=none --cpuctxsw=none --duration="+duration+" --gpu-metrics-devices=0 --gpu-metrics-frequency=1000 --output=\""+Path.Combine(dir,"gpu")+"\"";
                nsys=new Child(nsysPath,args,Path.Combine(dir,"nsight.log"));
            }
            state["status"]="recording";state["recording_started_utc"]=Utc();Save(receipt,state);
            var clock=Stopwatch.StartNew();
            while(clock.Elapsed.TotalSeconds<duration+(gpu?5:0) && !File.Exists(cancel)) Thread.Sleep(100);
            bool cancelled=File.Exists(cancel);
            if(cancelled && nsys!=null) { nsys.Dispose();nsys=null; }
            state["status"]="finalizing";state["recording_stop_requested_utc"]=Utc();Save(receipt,state);
            // Stop CPU recording before waiting for Nsight conversion.
            if(owned) {
                if(RunWpr("-stop \""+Path.Combine(dir,"cpu.etl")+"\"",dir,"wpr-stop.log",60000)!=0) throw new IOException("WPR stop failed");
                owned=false;
            }
            if(nsys!=null && nsys.Wait(90000)!=0) throw new IOException("Nsight failed; see protected log");
            state["status"]=cancelled?"cancelled":"completed";
        } catch(Exception e) { state["status"]="failed";state["error"]=e.Message; }
        finally {
            if(nsys!=null) nsys.Dispose();
            if(owned) {
                try { state["recovery_stop_exit"]=RunWpr("-stop \""+Path.Combine(dir,"recovered.etl")+"\"",dir,"wpr-recovery.log",60000); }
                catch { state["recovery_stop_exit"]=-1; }
                // Cancel only our named instance if recovery did not stop it.
                if(!state.ContainsKey("recovery_stop_exit") || Convert.ToInt32(state["recovery_stop_exit"])!=0)
                    try { state["recovery_cancel_exit"]=RunWpr("-cancel",dir,"wpr-cancel.log",15000); } catch { state["recovery_cancel_exit"]=-1; }
            }
            state["finished_utc"]=Utc();Save(receipt,state);
        }
    }
    static string ReadRequest(Stream stream) {
        var bytes=new byte[32];int count=0;var clock=Stopwatch.StartNew();
        while(count<bytes.Length) {
            int remaining=2000-(int)clock.ElapsedMilliseconds;
            if(remaining<=0) throw new TimeoutException("Request timed out");
            var read=stream.ReadAsync(bytes,count,1);
            if(!read.Wait(remaining)) throw new TimeoutException("Request timed out");
            if(read.Result==0) throw new IOException("Incomplete request");
            if(bytes[count]==10) return Encoding.ASCII.GetString(bytes,0,count);
            if(bytes[count]<32 || bytes[count]>126) throw new IOException("Invalid request encoding");
            count++;
        }
        throw new IOException("Request too long");
    }
    static NamedPipeServerStream CreatePipe(string sid,string name) {
        var acl=new PipeSecurity();
        acl.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.NetworkSid,null),PipeAccessRights.FullControl,AccessControlType.Deny));
        acl.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(sid),PipeAccessRights.ReadWrite,AccessControlType.Allow));
        acl.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null),PipeAccessRights.FullControl,AccessControlType.Allow));
        acl.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null),PipeAccessRights.FullControl,AccessControlType.Allow));
        // First-instance creation plus a single persistent pipe prevents a
        // second server attaching while the broker disconnects between clients.
        byte[] descriptor=acl.GetSecurityDescriptorBinaryForm();IntPtr sd=Marshal.AllocHGlobal(descriptor.Length);
        Microsoft.Win32.SafeHandles.SafePipeHandle pipeHandle;
        try {
            Marshal.Copy(descriptor,0,sd,descriptor.Length);
            var attributes=new SecurityAttributes { Length=Marshal.SizeOf(typeof(SecurityAttributes)),Descriptor=sd,Inherit=false };
            // DUPLEX | OVERLAPPED | FIRST_PIPE_INSTANCE; reject remote clients.
            pipeHandle=CreateNamedPipe(@"\\.\pipe\"+name,0x40080003,0x8,1,4096,4096,0,ref attributes);
            if(pipeHandle.IsInvalid) throw new IOException("Cannot claim private profiling pipe",new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()));
        } finally { Marshal.FreeHGlobal(sd); }
        return new NamedPipeServerStream(PipeDirection.InOut,true,false,pipeHandle);
    }
    static void Serve() {
        RequireInstalledAdmin();var cfg=Config();string sid=(string)cfg["sid"];
        if(WindowsIdentity.GetCurrent().User.Value!=sid) throw new UnauthorizedAccessException("Wrong task principal");
        using(var pipe=CreatePipe(sid,PipeName(sid))) {
            // A killed/crashed prior instance can leave ETW recording. This
            // recovery targets only the application's reserved instance name.
            RunWpr("-cancel",Root,"startup-recovery-"+Guid.NewGuid().ToString("N")+".log",15000);
            var state=new Dictionary<string,object> { {"status","ready"},{"broker_pid",Process.GetCurrentProcess().Id},{"started_utc",Utc()},{"expires_utc",DateTime.UtcNow.AddMinutes(30).ToString("o")},{"captures_started",0} };
            string receipt=Path.Combine(Root,"broker.json");Save(receipt,state);
            Process worker=null;Job workerJob=null;string current=null;bool ending=false;int captures=0;
            var age=Stopwatch.StartNew();Task accept=null;DateTime? endingAt=null;
            try {
                while(true) {
                    if(worker!=null && worker.HasExited) {
                        state["last_job"]=current;state["last_result"]=Load(Path.Combine(Root,"recordings",current,"result.json"));
                        worker.Dispose();worker=null;workerJob.Dispose();workerJob=null;state["status"]="ready";state.Remove("current_job");Save(receipt,state);
                    }
                    if(age.Elapsed.TotalMinutes>=30) ending=true;
                    if(ending) {
                        if(endingAt==null) endingAt=DateTime.UtcNow;
                        if(worker==null) break;
                        if((DateTime.UtcNow-endingAt.Value).TotalSeconds>=280) break;
                        string cancel=Path.Combine(Root,"recordings",current,"cancel.flag");
                        if(!File.Exists(cancel)) File.WriteAllText(cancel,"Session ending");
                    }
                    if(accept==null) accept=pipe.WaitForConnectionAsync();
                    if(!accept.Wait(200)) continue;
                    accept.GetAwaiter().GetResult();accept=null;
                    try {
                        string command=ReadRequest(pipe);
                        if(!ValidCommand(command)) throw new ArgumentException("Unsupported operation");
                        if(command=="shutdown") ending=true;
                        else if(command=="stop" && worker!=null) File.WriteAllText(Path.Combine(Root,"recordings",current,"cancel.flag"),"Stop requested");
                        else if(Array.IndexOf(Modes,command)>=0) {
                            if(ending || worker!=null || captures>=4) throw new InvalidOperationException("Session ending, busy, or capture limit reached");
                            CheckCaptureSpace(); // No output directory/job if the volume is low.
                            current=Guid.NewGuid().ToString("N");string dir=Path.Combine(Root,"recordings",current);Directory.CreateDirectory(dir);
                            // Only constants and a server-generated GUID enter arguments.
                            workerJob=new Job();worker=Process.Start(new ProcessStartInfo(Exe,"worker "+command+" "+current) {UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Root});workerJob.Add(worker);
                            captures++;state["captures_started"]=captures;state["status"]="busy";state["current_job"]=current;Save(receipt,state);
                        }
                        var response=new Dictionary<string,object>(state);response["ending"]=ending;
                        if(worker!=null) { string file=Path.Combine(Root,"recordings",current,"result.json");if(File.Exists(file)) response["current_result"]=Load(file); }
                        WriteResponse(pipe,response);
                    } catch { try { WriteResponse(pipe,new Dictionary<string,object>{{"error","Request rejected"}}); } catch {} }
                    finally { if(pipe.IsConnected) pipe.Disconnect(); }
                }
            } finally {
                if(worker!=null) {
                    File.WriteAllText(Path.Combine(Root,"recordings",current,"cancel.flag"),"Broker shutdown");
                    if(!worker.WaitForExit(endingAt==null?280000:1000)) {
                        workerJob.Dispose();
                        try { RunWpr("-cancel",Root,"emergency-"+Guid.NewGuid().ToString("N")+".log",15000); } catch {}
                    }
                    worker.Dispose();
                }
                if(workerJob!=null) workerJob.Dispose();state["status"]="stopped";state["finished_utc"]=Utc();Save(receipt,state);
            }
        }
    }
    static void WriteResponse(Stream stream,object data) {
        byte[] bytes=Encoding.UTF8.GetBytes(Json.Serialize(data)+"\n");var write=stream.WriteAsync(bytes,0,bytes.Length);
        if(!write.Wait(2000)) throw new TimeoutException("Response timed out");write.GetAwaiter().GetResult();
    }
    static int Client(string command) {
        if(!ValidCommand(command)) throw new ArgumentException("Unsupported operation");
        string sid=WindowsIdentity.GetCurrent().User.Value;
        var cfg=Config();if((string)cfg["sid"]!=sid) throw new UnauthorizedAccessException("Wrong user");
        var state=Load(Path.Combine(Root,"broker.json"));
        using(var pipe=new NamedPipeClientStream(".",PipeName(sid),PipeAccessRights.ReadWrite,PipeOptions.Asynchronous,TokenImpersonationLevel.Identification,HandleInheritability.None)) {
            pipe.Connect(3000);uint server;
            if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle,out server) || server!=Convert.ToUInt32(state["broker_pid"])) throw new IOException("Untrusted pipe server");
            IntPtr process=OpenProcess(0x1000,false,server);
            try {
                var image=new StringBuilder(32768);uint size=(uint)image.Capacity;
                if(process==IntPtr.Zero || !QueryFullProcessImageName(process,0,image,ref size) || !String.Equals(image.ToString(),Exe,StringComparison.OrdinalIgnoreCase)) throw new IOException("Untrusted pipe executable");
            } finally { if(process!=IntPtr.Zero) CloseHandle(process); }
            byte[] bytes=Encoding.ASCII.GetBytes(command+"\n");pipe.Write(bytes,0,bytes.Length);pipe.Flush();
            var response=new byte[32768];int count=0;
            while(count<response.Length) {
                var read=pipe.ReadAsync(response,count,response.Length-count);
                if(!read.Wait(5000)) throw new TimeoutException("No response");int got=read.Result;if(got==0) break;count+=got;
                if(Array.IndexOf(response,(byte)10,0,count)>=0) break;
            }
            string text=Encoding.UTF8.GetString(response,0,count).Trim();Console.WriteLine(text);
            return Json.Deserialize<Dictionary<string,object>>(text).ContainsKey("error")?1:0;
        }
    }
    static int Tests() {
        foreach(long available in new long[] {-1,0,CaptureReserveBytes-1}) {
            bool refused=false;
            try { RequireCaptureSpace(available); } catch(IOException) { refused=true; }
            if(!refused) throw new Exception("Low-space capture accepted");
        }
        RequireCaptureSpace(CaptureReserveBytes);
        RequireCaptureSpace(CaptureReserveBytes+1);
        foreach(string command in Commands) if(!ValidCommand(command)) throw new Exception("Valid command rejected");
        foreach(string command in new[]{"","CPU30","cpu31","cpu30 & whoami","../../cpu3","cpu3\nshutdown","powershell","gpu0",new string('x',33)}) if(ValidCommand(command)) throw new Exception("Invalid command accepted");
        using(var stream=new MemoryStream(Encoding.ASCII.GetBytes("cpu3\n"))) if(ReadRequest(stream)!="cpu3") throw new Exception("Framing");
        foreach(byte[] bytes in new[]{Encoding.ASCII.GetBytes(new string('x',33)),new byte[]{0,10},Encoding.ASCII.GetBytes("cpu3")}) {
            bool rejected=false;try { using(var stream=new MemoryStream(bytes)) ReadRequest(stream); }catch {rejected=true;}if(!rejected) throw new Exception("Unsafe frame accepted");
        }
        string sid=WindowsIdentity.GetCurrent().User.Value,name="BruteForceProfiler-selftest-"+Guid.NewGuid().ToString("N");
        using(var server=CreatePipe(sid,name)) {
            bool duplicateRejected=false;try { using(var duplicate=CreatePipe(sid,name)) {} }catch(IOException) { duplicateRejected=true; }
            if(!duplicateRejected) throw new Exception("Duplicate pipe server accepted");
            var connected=server.WaitForConnectionAsync();
            using(var client=new NamedPipeClientStream(".",name,PipeAccessRights.ReadWrite,PipeOptions.Asynchronous,TokenImpersonationLevel.Identification,HandleInheritability.None)) {
                client.Connect(3000);if(!connected.Wait(3000)) throw new Exception("Pipe connection");
                byte[] bytes=Encoding.ASCII.GetBytes("status\n");client.Write(bytes,0,bytes.Length);
                if(ReadRequest(server)!="status") throw new Exception("Live pipe request");
                uint process;if(!GetNamedPipeServerProcessId(client.SafePipeHandle,out process) || process!=(uint)Process.GetCurrentProcess().Id) throw new Exception("Server identity");
                WriteResponse(server,new Dictionary<string,object>{{"status","test"}});
                var response=new byte[128];var read=client.ReadAsync(response,0,response.Length);
                if(!read.Wait(3000) || !Encoding.UTF8.GetString(response,0,read.Result).Contains("test")) throw new Exception("Live pipe response");
            }
        }
        Console.WriteLine("Protocol allowlist, injection rejection, size, encoding, EOF, live pipe round-trip, server identity and duplicate-server rejection passed");return 0;
    }
    static int Main(string[] args) {
        try {
            if(args.Length==1 && args[0]=="selftest") return Tests();
            if(args.Length==1 && args[0]=="serve") {Serve();return 0;}
            if(args.Length==3 && args[0]=="worker") {Work(args[1],args[2]);return 0;}
            if(args.Length==2 && args[0]=="client") return Client(args[1]);
            throw new ArgumentException("Expected serve, worker, client or selftest");
        } catch(Exception e) {Console.Error.WriteLine(e.GetType().Name+": "+e.Message);return 1;}
    }
}
