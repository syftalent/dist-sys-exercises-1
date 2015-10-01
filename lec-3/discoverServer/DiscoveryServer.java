import java.net.Socket;
import java.net.ServerSocket;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.UnknownHostException;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.HashMap;
import java.util.regex.Pattern;
import java.util.*;


public class DiscoveryServer {
	private static ServerContainer mServerContainer;
	private static Map<String,ConObject> mObjMap;
	
	// a simple function to check is Integer or not
	private static boolean isInteger(String str) {    
        Pattern pattern = Pattern.compile("^[-\\+]?[\\d]*$");    
        return pattern.matcher(str).matches();    
    }
	
	public static void process (Socket clientSocket) throws IOException {
        // open up IO streams
        BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
        PrintWriter out = new PrintWriter(clientSocket.getOutputStream(), true);

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
        switch(tokens[0].toUpperCase()){
            // Add a new server
            case "ADD":{
                if(tokens.length != 5){
                    out.println("Failure: Number of parameter should be five");
                    break;
                }
                if(!isInteger(tokens[4])){
                    out.println("Failure: Port num should be interger");
                    break;
                }
                
                ConObject obj1 = (mObjMap.containsKey(tokens[1])) ? 
                    mObjMap.get(tokens[1]):new ConObject(tokens[1]);
                ConObject obj2 = (mObjMap.containsKey(tokens[2])) ? 
                    mObjMap.get(tokens[2]):new ConObject(tokens[2]);
                
                if(!mObjMap.containsKey(obj1.name))
                    mObjMap.put(obj1.name, obj1);
                if(!mObjMap.containsKey(obj2.name))
                    mObjMap.put(obj2.name, obj2);
                    
                String ip = tokens[3];
                int port = Integer.parseInt(tokens[4]);
                
                Server server = new Server(ip, port, obj1, obj2);
                if(mServerContainer.addServer(server)){
                    out.println("Sucess.");
                }else{
                    out.println("Failure:Add Server Failed");
                }
                break;
            }
            // remove server from server table
            case "REMOVE":{
                if(tokens.length != 3){
                    out.println("Failure: Number of parameter should be three");
                    break;
                }
                if(!isInteger(tokens[2])){
                    out.println("Failure: Port num should be interger");
                    break;
                }
                
                String ip = tokens[1];
                int port = Integer.parseInt(tokens[2]);
                if(mServerContainer.removeServer(ip, port)){
                    out.println("Sucess.");
                }else{
                    out.println("Failure.");
                }
                break;
            }
            // look up server from server table
            case "LOOKUP":{
                if(tokens.length != 3){
                    out.println("Failure: Number of parameter should be three");
                    break;
                }
                if(!mObjMap.containsKey(tokens[1]) || !mObjMap.containsKey(tokens[2])){
                    out.println("Unit not existed.");
                    break;
                }
                ConObject obj1 = mObjMap.get(tokens[1]);
                ConObject obj2 = mObjMap.get(tokens[2]);
                
                Server server = mServerContainer.getOneServer(obj1,obj2);
                if(server == null){
                    out.println("Failure: No available server");
                }else{
                    out.println(server.ip + " " + server.port);
                }
                break;
            }
            // look up all the server which match the requirement of the client
            case "LOOKUP_MULTI":{
                if(tokens.length != 3){
                    out.println("Failure: Number of parameter should be three");
                    break;
                }
                if(!mObjMap.containsKey(tokens[1]) || !mObjMap.containsKey(tokens[2])){
                    out.println("Unit not existed.");
                    break;
                }
                ConObject obj1 = mObjMap.get(tokens[1]);
                ConObject obj2 = mObjMap.get(tokens[2]);
                
                LinkedHashSet<ConObject> path = findConnectRoute(obj1, obj2, new LinkedHashSet<ConObject>());
                if(path.size() == 0){
                    out.println("Path not existed.");
                    break;
                }
                
                Iterator<ConObject> iter = path.iterator();
                obj1 = null;
                obj2 = iter.next();
                while(iter.hasNext()){
                    obj1 = obj2;
                    obj2 = iter.next();
                    Server server = mServerContainer.getOneServer(obj1, obj2);
                    if(server == null){
                        out.println("Server searching failed");
                    }else{
                        out.println(obj1.name + " " + obj2.name + " " 
                            + server.ip + " " + server.port + " " + iter.hasNext());
                    }
                }
                break;
            }
            default:
                out.println("Invalid Input!");
        }
            
        // close IO streams, then socket
        out.close();
        in.close();
        clientSocket.close();       
    }
	
	private static LinkedHashSet<ConObject> findConnectRoute(ConObject obj1, ConObject obj2, LinkedHashSet<ConObject> connectRoute){
        connectRoute.add(obj1);
        if(obj1 == obj2)
            return connectRoute;
        
        Iterator<ConObject> iter = obj1.getConnectedObjs().iterator(); 
        while(iter.hasNext()){
            ConObject nextObj = iter.next();
            if(connectRoute.contains(nextObj))
                continue;
            else{
                LinkedHashSet<ConObject> temp = findConnectRoute(nextObj,obj2,connectRoute);
                if(temp.contains(obj2))
                    return temp;
            }
        }
        connectRoute.remove(obj1);
        return connectRoute;
    }
	
	public static void main(String[] args) throws Exception {

        //check if argument length is invalid
        if(args.length != 1) {
            System.err.println("Usage: java ConvServer port");
            System.exit(-1);
        }
        
        mServerContainer = new ServerContainer();
  	    mObjMap = new HashMap<String,ConObject>();
        
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