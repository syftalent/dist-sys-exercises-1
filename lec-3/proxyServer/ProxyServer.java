
/******************************************************************************
 *
 *  CS 6421 - Proxy Conversation Server
 *  Compilation:  javac ProxyServer.java
 *  Execution:    java ProxyServer port
 *  Programmer: Yifei Shen, Huida Tao, Jizi Shangguan
 * 
 ******************************************************************************/
 
import java.net.Socket;
import java.net.ServerSocket;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.regex.Pattern;
import java.io.*;

public class ProxyServer {
    //constant value 
    //unit this program may resolve
    //ip address of Servers
    //port num of Servers
    private static String discoveryHost = "52.89.49.207";
    private static int discoveryPort = 1111;
    
    public static void process (Socket clientSocket) throws IOException {
        // open up IO streams
        BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
        PrintWriter out = new PrintWriter(clientSocket.getOutputStream(), true);

        /* Write a welcome message to the client */
        out.println("Welcome to the Proxy Server!");
        out.println("Please input as \"<input> <output> <value>\"");
        
        
        /* read and print the client's request */
        // readLine() blocks until the server receives a new line from client
        String userInput;
        if ((userInput = in.readLine()) == null) {
            System.out.println("Error reading message");
            out.close();
            in.close();
            clientSocket.close();
        }

        System.out.println("Received message: " + userInput);
        //--TODO: add your converting functions here, msg = func(userInput);
        
        String[] tokens = userInput.split(" ");
        
        //Check the input and call other servers
        if(tokens.length != 3){
            out.println("The number of parameter is incorrect");
        }else if(!isDouble(tokens[2])){
            out.println("The third parameter is not interger");
        }else{
             Server server = lookupServer(tokens);
             String input = tokens[0] + " " + tokens[1] + " " + tokens[2];
             if(server != null){
                String result = server.connectServer(server.ip, server.port, input);
                out.println(result);
             }else{
                 out.println("This conversion do not exist");
             }
        }
        
        // close IO streams, then socket
        out.close();
        in.close();
        clientSocket.close();
    }
    
    // function to connect the discovery server and get back the result as a Server object
    public static Server lookupServer(String[] tokens) throws IOException{
    	 String obj1 = tokens[0];
         String obj2 = tokens[1];
         Server result = null;
         
         Socket discoverySocket;
         try{
         	discoverySocket = new Socket(discoveryHost, discoveryPort);
         }catch(IOException e){
         	System.err.println("Failed to find discovery server!");
         	return null;
         }
         
         PrintWriter dout = new PrintWriter(discoverySocket.getOutputStream(),true);
         BufferedReader din = new BufferedReader(new InputStreamReader(discoverySocket.getInputStream()));
         
         dout.println("LOOKUP" + " " + obj1 + " " + obj2);
         String output;
         switch (output = din.readLine()){
         	case "ERR001" :{
         		System.out.println(Constants.getErrInfoString("ERR001"));
         		break;
         	}
         	case "ERR008" :{
         		System.out.println(Constants.getErrInfoString("ERR008"));
         		break;
         	}
         	case "ERR009" :{
         		System.out.println(Constants.getErrInfoString("ERR009"));
         		break;
         	}
         	default :{
                System.out.println(output);
                String[] answer = output.split(" ");
                result = new Server(answer[0], Integer.parseInt(answer[1]), new ConObject(obj1), new ConObject(obj2));
         	}
         }
         
         //System.out.println(result.ip + result.port);
         return result;
         
    }
    
    // a simple function to check is Double or not
	//if not return null,if is return double
	private static boolean isDouble(String str) {    
        try{
            Double.parseDouble(str);
        }catch(NumberFormatException e){
            return false;
        }
        return true;
    }
    
    public static void main(String[] args) throws Exception {

        //check if argument length is invalid
        if(args.length != 1) {
            System.err.println("Usage: java ConvServer port");
            System.exit(-1);
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