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
	static List<String[]> serverTable = new ArrayList<String[]>();
	
	// add new server into the server table
	public static void add(String obj1, String obj2, String host, int port){
		String[] newServer1 = {obj1, obj2, host, Integer.toString(port)};
		String[] newServer2 = {obj2, obj1, host, Integer.toString(port)};
		serverTable.add(newServer1);
		serverTable.add(newServer2);
	}
	
	// delete the server from server table	
	public static void remove(String host, int port){
		/*int tableSize = serverTable.size();
		for (int i = 0; i < tableSize; i ++){
			String[] temp = serverTable.get(i);
			if(temp[2].equals(host) && temp[3].equals(Integer.toString(port))){
				serverTable.remove(i);
			}
		}*/
		Iterator<String[]> iter = serverTable.iterator();
		while(iter.hasNext()){
		    String[] temp = iter.next();
		    if(temp[2].equals(host) && temp[3].equals(Integer.toString(port))){
		        iter.remove();
		        serverTable.remove(temp);
		    }
		}
	}
	
	// look up for certain server
	public static List<String[]> lookup(String obj1, String obj2) {
		int tableSize = serverTable.size();
		List<String[]> result = new ArrayList<String[]>();
		for (int i = 0; i < tableSize; i ++){
			String[] temp = serverTable.get(i);
			if(temp[0].equals(obj1) && temp[1].equals(obj2)){
				result.add(serverTable.get(i));
			}
		}
		return result;
	}
	
	// a simple function to check is Integer or not
	private static boolean isInteger(String str) {    
        Pattern pattern = Pattern.compile("^[-\\+]?[\\d]*$");    
        return pattern.matcher(str).matches();    
    }
	
	public static void process (Socket clientSocket) throws IOException {
        // open up IO streams
        BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
        PrintWriter out = new PrintWriter(clientSocket.getOutputStream(), true);

        /* Write a welcome message to the client */
        out.println("Welcome to the Discovery Server!");
        
        out.println("Protocols:");
        out.println("To add a server: ADD <Unit1> <Unit2> <hostname> <port>");
        out.println("To look up a server: LOOKUP <Unit1> <Unit2>");
        out.println("To remove a server: REMOVE <hostname> <port>");

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
        if (tokens.length == 5 && tokens[0].equals("ADD") && isInteger(tokens[4])){
            String obj1 = tokens[1];
            String obj2 = tokens[2];
            String host = tokens[3];
            int port = Integer.parseInt(tokens[4]);
            add(obj1, obj2, host, port);
        }else if (tokens.length == 3 && tokens[0].equals("REMOVE") && isInteger(tokens[2])){
            remove(tokens[1], Integer.parseInt(tokens[2]));
            
        }else if (tokens.length == 3 && tokens[0].equals("LOOKUP")){
            List<String[]> result = lookup(tokens[1], tokens[2]);
            if(result.size() == 0){
            	out.println("NULL");
            }else{
            	int listSize = result.size();
            	for(int i = 0; i < listSize; i ++){
            		String[] singleResult = result.get(i);
            		out.println(singleResult[2] + " " + singleResult[3]);
            	}
            }
        }else{
            out.println("Invalid Input!");
        }
        // close IO streams, then socket
        out.close();
        in.close();
        clientSocket.close();       
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
