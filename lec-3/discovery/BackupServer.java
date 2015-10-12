import java.net.Socket;
import java.net.ServerSocket;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.UnknownHostException;
import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.HashMap;
import java.util.regex.Pattern;
import java.util.*;

public class BackupServer{
    private static ServerContainer backupContainer = null;
    
    private static void process (Socket client) throws IOException {
        ObjectOutputStream os = null;
        ObjectInputStream is = null;
        
        try{
            
            os = new ObjectOutputStream(client.getOutputStream());
            is = new ObjectInputStream(new BufferedInputStream(client.getInputStream()));    	
            Object obj = is.readObject();
            ServerContainer s = (ServerContainer)obj;
            if(s.ipportServerContainer.containsKey("null")){
                System.err.println("null input");
            }
            else{
                ServerContainer backupContainer = (ServerContainer)obj;
                System.out.println(backupContainer.ipportServerContainer.get("qqq:4444").ip);
                
            }
        
            os.writeObject(backupContainer);
            os.flush();
            System.out.println("wirteback finish");
        }catch(IOException e){
            System.err.println("err1");
        }catch(ClassNotFoundException ex){
            System.err.println("err2");
        }
        
        
        is.close();
        os.close();

        client.close();
    }
    
    public static void main(String[] args) throws Exception {

        //check if argument length is invalid
        if(args.length != 1) {
            System.err.println("Usage: java ConvServer port");
        }
        // create socket
        int port = Integer.parseInt(args[0]);
        ServerSocket serverSocket = new ServerSocket(port);
        System.err.println("Started server on port " + port);
        
        // wait for connections, and process
        try {
            while(true) {
                // a "blocking" call which waits until a connection is requested
                Socket clientSocket = serverSocket.accept();
                System.err.println("\nAccepted connection from client");
                process(clientSocket);
                
            }

        }catch (IOException e) {
            System.err.println("Connection Error");
        }
        System.exit(0);
    }
}