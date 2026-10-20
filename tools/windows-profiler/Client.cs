// Ordinary, unprivileged transport. Updating this client cannot expand the
// immutable elevated broker's fixed operation allowlist.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
internal static class Client {
    const string Root=@"C:\ProgramData\BruteForceProfiler";
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static readonly string[] Commands={"status","stop","shutdown","cpu3","cpu30","gpu3","gpu30","combat30"};
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe,out uint pid);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process,uint flags,StringBuilder path,ref uint size);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static NamedPipeClientStream Connect(string name) {
        var pipe=new NamedPipeClientStream(".",name,PipeAccessRights.ReadWrite,PipeOptions.Asynchronous,TokenImpersonationLevel.Identification,HandleInheritability.None);
        try {pipe.Connect(3000);return pipe;} catch {pipe.Dispose();throw;}
    }
    static string Exchange(NamedPipeClientStream pipe,string command) {
        // Arm the read before sending the request. The installed broker's
        // disconnect can discard a buffered reply if no reader is pending.
        var response=new byte[32768];int count=0;
        var read=pipe.ReadAsync(response,0,response.Length);
        byte[] bytes=Encoding.ASCII.GetBytes(command+"\n");pipe.Write(bytes,0,bytes.Length);pipe.Flush();
        var clock=Stopwatch.StartNew();
        while(count<response.Length) {
            int remaining=5000-(int)clock.ElapsedMilliseconds;
            if(remaining<=0 || !read.Wait(remaining)) throw new TimeoutException("No complete broker response; check status before retrying a capture");
            if(read.Result==0) break;count+=read.Result;
            if(Array.IndexOf(response,(byte)10,0,count)>=0) return Encoding.UTF8.GetString(response,0,count).Trim();
            read=pipe.ReadAsync(response,count,response.Length-count);
        }
        throw new IOException("Incomplete broker response; check status before retrying a capture");
    }
    static int Request(string command) {
        if(Array.IndexOf(Commands,command)<0) throw new ArgumentException("Unsupported operation");
        string sid=WindowsIdentity.GetCurrent().User.Value;
        var config=Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(Root,"config.json")));
        if((string)config["sid"]!=sid) throw new UnauthorizedAccessException("Wrong user");
        var state=Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(Root,"broker.json")));
        using(var pipe=Connect("BruteForceProfiler-v1-"+sid)) {
            uint server;
            if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle,out server) || server!=Convert.ToUInt32(state["broker_pid"])) throw new IOException("Untrusted pipe server");
            IntPtr process=OpenProcess(0x1000,false,server);
            try {
                var path=new StringBuilder(32768);uint size=(uint)path.Capacity;
                if(process==IntPtr.Zero || !QueryFullProcessImageName(process,0,path,ref size) || !String.Equals(path.ToString(),Path.Combine(Root,"Profiler.exe"),StringComparison.OrdinalIgnoreCase)) throw new IOException("Untrusted pipe executable");
            }finally {if(process!=IntPtr.Zero) CloseHandle(process);}
            string text=Exchange(pipe,command);var result=Json.Deserialize<Dictionary<string,object>>(text);
            if(result==null) throw new IOException("Empty response");Console.WriteLine(text);return result.ContainsKey("error")?1:0;
        }
    }
    static void Tests() {
        for(int i=0;i<50;i++) {
            string name="BruteForceProfiler-client-test-"+Guid.NewGuid().ToString("N");
            using(var server=new NamedPipeServerStream(name,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous)) {
                var connected=server.WaitForConnectionAsync();
                var responder=Task.Run(delegate {
                    if(!connected.Wait(3000)) throw new Exception("Test connection timeout");
                    var request=new byte[32];int n=0;
                    while(n<request.Length) {int got=server.Read(request,n,1);if(got==0) throw new Exception("Missing request");if(request[n++]==10) break;}
                    byte[] bytes=Encoding.ASCII.GetBytes("{\"status\":\"ready\"}\n");server.Write(bytes,0,bytes.Length);
                    server.Disconnect(); // Intentionally exercise the production race.
                });
                using(var client=Connect(name)) if(Exchange(client,"status")!="{\"status\":\"ready\"}") throw new Exception("Reply lost");
                if(!responder.Wait(3000)) throw new Exception("Responder timeout");responder.GetAwaiter().GetResult();
            }
        }
        Console.WriteLine("Client reply/disconnect regression: 50 live pipe exchanges passed");
    }
    static int Main(string[] args) {
        try {if(args.Length!=1) throw new ArgumentException("Expected one operation");if(args[0]=="selftest"){Tests();return 0;}return Request(args[0]);}
        catch(Exception e){Console.Error.WriteLine(e.GetType().Name+": "+e.Message);return 1;}
    }
}
