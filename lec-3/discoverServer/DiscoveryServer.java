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
//import discoverServer.ConObject;


public class DiscoveryServer {
	static ServerContainer sc = new ServerobjServerContainer();
	
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
        if (tokens.length == 5 && tokens[0].equals("ADD") && isInteger(tokens[4])){
            Server server = new Server(tokens[1], tokens[2], tokens[3], tokens[4]);
            int port = Integer.parseInt(tokens[4]);
            addServer(server);
        }else if (tokens.length == 3 && tokens[0].equals("REMOVE") && isInteger(tokens[2])){
            if(removeServer(tokens[1], Integer.parseInt(tokens[2]))){
                out.println("Sucess.");
            }else{
                out.println("Failure.");
            };
            
        }else if (tokens.length == 3 && tokens[0].equals("LOOKUP")){
            ConObject obj1 = new ConObject(tokens[1]);
            ConObject obj2 = new ConObject(tokens[2]);
            Server server = getOneServer(obj1, obj2);
            if(server == null){
                out.println("Not exist.");
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

