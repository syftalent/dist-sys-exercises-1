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
        
        //check if all servers are online
        Iterator<Map.Entry<String, Set<Server>>> serverEntries = mServerContainer.getServerTable().entrySet().iterator();
        while(serverEntries.hasNext()){
            Map.Entry<String, Set<Server>> entry = serverEntries.next();
            Set<Server> hs = entry.getValue();
            Iterator<Server> iter = hs.iterator();
            while(iter.hasNext()){
                Server server = iter.next();
                try{
                    checkServer(server);
                }catch(IOException e){
                    
                }
            }
        }

        /* read and print the client's request */
        // readLine() blocks until the server receives a new line from client
        String userInput;
        if ((userInput = in.readLine()) == null) {
            System.out.println(Constants.getErrInfoString("ERR004"));
            out.println("ERR004");
            out.close();
            in.close();
            clientSocket.close();
            return;
        }

        System.out.println("Received message: " + userInput);
        //--TODO: add your converting functions here, msg = func(userInput);
        
        
        String[] tokens = userInput.split(" ");
        switch(tokens[0]){
            // Add a new server
            case "ADD":{
                if(tokens.length != 5){
                    out.println("ERR001");
                    System.out.println(Constants.getErrInfoString("ERR001") + "*ADD");
                    break;
                }
                if(!isInteger(tokens[4])){
                    out.println("ERR006");
                    System.out.println(Constants.getErrInfoString("ERR006") + "*ADD");
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
                    out.println("SUC");
                    System.out.println(Constants.getErrInfoString("SUC") + "*ADD");
                }else{
                    out.println("ERR007");
                    System.out.println(Constants.getErrInfoString("ERR007") + "*ADD");
                }
                break;
            }
            // remove server from server table
            case "REMOVE":{
                if(tokens.length != 3){
                    out.println("ERR001");
                    System.out.println(Constants.getErrInfoString("ERR001") + "*REMOVE");
                    break;
                }
                if(!isInteger(tokens[2])){
                    out.println("ERR010");
                    System.out.println(Constants.getErrInfoString("ERR010") + "*REMOVE");
                    break;
                }
                
                String ip = tokens[1];
                int port = Integer.parseInt(tokens[2]);
                if(mServerContainer.removeServer(ip, port)){
                    out.println("SUC");
                    System.out.println(Constants.getErrInfoString("SUC") + "*REMOVE");
                }else{
                    out.println("ERR011");
                    System.out.println(Constants.getErrInfoString("ERR011") + "*REMOVE");
                }
                break;
            }
            // look up server from server table
            case "LOOKUP":{
                if(tokens.length != 3){
                    out.println("ERR001");
                    System.out.println(Constants.getErrInfoString("ERR001") + "*LOOKUP");
                    break;
                }
                if(!mObjMap.containsKey(tokens[1]) || !mObjMap.containsKey(tokens[2])){
                    out.println("ERR008");
                    System.out.println(Constants.getErrInfoString("ERR008") + "*LOOKUP");
                    break;
                }
                ConObject obj1 = mObjMap.get(tokens[1]);
                ConObject obj2 = mObjMap.get(tokens[2]);
                
                Server server = mServerContainer.getOneServer(obj1,obj2);
                if(server == null){
                    out.println("ERR009");
                    System.out.println(Constants.getErrInfoString("ERR009") + "*LOOKUP");
                }else{
                    out.println(server.ip + " " + server.port);
                    // put the used server at the back of the server set
                    if(mServerContainer.removeServer(server.ip, server.port)){
                        mServerContainer.addServer(server);
                    }
                }
                
                
                
                break;
            }
            // look up all the server which match the requirement of the client
            case "LOOKUP_MULTI":{
                if(tokens.length != 3){
                    out.println("ERR001");
                    System.out.println(Constants.getErrInfoString("ERR001") + "*LOOKUP_MULTI");
                    break;
                }
                if(!mObjMap.containsKey(tokens[1]) || !mObjMap.containsKey(tokens[2])){
                    out.println("ERR008");
                    System.out.println(Constants.getErrInfoString("ERR008") + "*LOOKUP_MULTI");
                    break;
                }
                ConObject obj1 = mObjMap.get(tokens[1]);
                ConObject obj2 = mObjMap.get(tokens[2]);
                
                LinkedHashSet<ConObject> path = findConnectRoute(obj1, obj2, new LinkedHashSet<ConObject>());
                if(path.size() == 0){
                    out.println("ERR012");
                    System.out.println(Constants.getErrInfoString("ERR012") + "*LOOKUP_MULTI");
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
                        out.println("ERR009");
                        System.out.println(Constants.getErrInfoString("ERR009") + "*LOOKUP_MULTI");
                    }else{
                        out.println(obj1.name + " " + obj2.name + " " 
                            + server.ip + " " + server.port + " " + iter.hasNext());
                    }
                }
                break;
            }
            default:
                out.println("ERR013");
                System.out.println(Constants.getErrInfoString("ERR013"));
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
    
    // check if server is online, if not, delete it from server table
    private static void checkServer(Server server) throws IOException {
        Socket checkSocket = null;
        try{
            checkSocket = new Socket(server.ip, server.port);
            PrintWriter out = new PrintWriter(checkSocket.getOutputStream(),true);
            out.println("check check 0");
            checkSocket.close();
        }catch(IOException e){
            mServerContainer.removeServer(server.ip, server.port);
            System.out.println("Delete off-line server: " + server.ip + ":"+server.port);
        }
        
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