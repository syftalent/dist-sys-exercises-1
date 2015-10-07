
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
    
    public static void process (Socket clientSocket) throws IOException {
        // open up IO streams
        BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
        PrintWriter out = new PrintWriter(clientSocket.getOutputStream(), true);

        /* Write a welcome message to the client */
        out.println("Welcome to the Proxy Server!");
        out.println("Please input as \"<input> <output> <value>\"");
        
        //traverse the osContainer
        Iterator<String> objIter = osContainer.objServer.keySet().iterator();
        while(objIter.hasNext()){
        	out.println("Available Unit: " + objIter.next());
        }
        
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
            
        
            /*Set<String> connectRoute = findConnectRoute(obj1,obj2,new LinkedHashSet<String>());
            if(connectRoute.size() == 0){
                out.println("The convertion is not available");
            }else if(connectRoute.size() < 2){
                System.err.println("Find Route Err");
            }else{
                Iterator<String> test = connectRoute.iterator();
                while(test.hasNext())
                    System.out.println(test.next());
                
                Iterator<String> routeIter = connectRoute.iterator();
                obj2 = routeIter.next();
                String output = null;
                while(routeIter.hasNext()){
                    obj1 = obj2;
                    obj2 = routeIter.next();
                    String input = obj1 + " " + obj2 + " " + val;
                    Server s = osContainer.getServer(obj1,obj2);
                    output = connectServer(s.host, s.port, input);
                    System.out.println(output);
                    if(output == null){
                        out.println("Connection Failed");
                        break;
                    }
                    String[] temp = output.split(" ");
                    val = temp[0];
                }
                out.println(output);
            }*/
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
         Server result;
         
         Socket discoverySocket;
         try{
         	discoverySocket = new Socket(discoveryHost, discoveryPort);
         }catch(IOException e){
         	System.err.println("Failed to find discovery server!");
         	return null;
         }
         
         PrintWriter dout = new PrintWriter(discoverySocket.getOutputStream(),true);
         BufferedReader din = new BufferedReader(new InputStreamReader(discoverySocket.getInputStream()));
         
         dout.println("lookup" + " " + tokens[0] + " " + tokens[1]);
         String output;
         switch (output = din.readLine()){
         	case "ERR001" :{
         		System.out.println(Constants.getErrInfoString("ERR001"));
         	}
         	case "ERR008" :{
         		System.out.println(Constants.getErrInfoString("Err008"));
         	}
         	case "ERR009" :{
         		System.out.println(Constants.getErrInfoString("ERR009"));
         	}
         	default :{
         	
         	}
         }
         String[] answer = output.split(" ");
         result = new Server(answer[0], Integer.parseInt(answer[1]), new ConObject(obj1), new ConObject(obj2));
         return result;
         
    }
    
    
    //function using to connect the remote server
    public static String connectServer(String host, int port, String input) throws IOException{
        Socket socket;
        
        try{
            socket = new Socket(host, port);
        }catch(IOException e){
            System.err.println("Conection Failed!");
            return null;
        }
        
        PrintWriter out = new PrintWriter(socket.getOutputStream(),true);
        BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        
        out.println(input);
        String output;
        if((output = in.readLine()) == null || output.equals("Invalid Input!")){
            System.err.println("Error reading message");
            out.close();
            in.close();
            socket.close();
            return null;
        }
        out.close();
        in.close();
        socket.close();
        return output;
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

        // //read server info from routing table file
        // //add server to ObjServerContainer
        // try{
        //     BufferedReader reader = new BufferedReader(new FileReader("routing_table"));
        //     String strIn = reader.readLine();
        //     while((strIn = reader.readLine()) != null){
        //         String[] split = strIn.split(" ");
        //         if(split.length != 4){
        //             System.err.println("routing_table row length invalid");
        //             continue;
        //         }
        //         if(!isInteger(split[1])){
        //             System.err.println("routing_table port not interger");
        //             continue;
        //         }
        //         String host = split[0];
        //         int port = Integer.parseInt(split[1]);
        //         String obj1 = split[2];
        //         String obj2 = split[3];
                
        //         Server s = new Server(host,port,obj1,obj2);
        //         osContainer.addServer(obj1,obj2,s);
        //         osContainer.addServer(obj2,obj1,s);
        //     }
        // }catch(FileNotFoundException fnfe){
        //     System.err.println(fnfe);
        // }catch(IOException ioe){
        //     System.err.println(ioe);
        // }