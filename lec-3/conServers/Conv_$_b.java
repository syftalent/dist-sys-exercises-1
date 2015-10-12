/******************************************************************************
 *
 *  CS 6421 - Simple Conversation
 *  Compilation:  javac Conv_$_b.java
 *  Execution:    java Conv_$_b port
 *
 *  % java Conv_$_b portnum
 ******************************************************************************/
 
import java.net.InetAddress;
import java.net.Socket;
import java.net.ServerSocket;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.UnknownHostException;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.regex.Pattern;

public class Conv_$_b {
    final static String discoverIp = "52.91.204.226";
    final static int discoverPort = 2222;

    private static void process (Socket clientSocket) throws IOException {
        // open up IO streams
        BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
        PrintWriter out = new PrintWriter(clientSocket.getOutputStream(), true);

        /* Write a welcome message to the client */
        //out.println("Welcome to the bananas (b) to pounds of bananas (lbs) conversion server!\n");

        /* read and print the client's request */
        // readLine() blocks until the server receives a new line from client
        String userInput;
        if ((userInput = in.readLine()) == null) {
            System.err.println("Reading input stream failed");
            out.close();
            in.close();
            clientSocket.close();
        }

        System.out.println("Received message: " + userInput);
        //--TODO: add your converting functions here, msg = func(userInput);
        
        String[] tokens = userInput.split(" ");
        if(tokens.length != 3){
            out.println("ERR001");
            System.out.println(Constants.getErrInfoString("ERR001"));
        }
        else if(!isDouble(tokens[2])){
            out.println("ERR002");
            System.out.println(Constants.getErrInfoString("ERR002"));
        }
        else{
            double result;
            double num = Double.parseDouble(tokens[2]);
            if (tokens[0].equals("$") && tokens[1].equals("b")){
                result = num * 2;
                out.println(result);
            }else if (tokens[0].equals("b") && tokens[1].equals("$")){
                result = num / 2;
                out.println(result);
            }else{
                out.println("ERR003");
                System.out.println(Constants.getErrInfoString("ERR003"));
            }
        }

        // close IO streams, then socket
        out.close();
        in.close();
        clientSocket.close();
    }
    
    private static boolean isInteger(String str) {    
        Pattern pattern = Pattern.compile("^[-\\+]?[\\d]*$");    
        return pattern.matcher(str).matches();    
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
            System.err.println("Do not have port number");
            System.exit(-1);
        }
        
        int port = 0;
        // create server socket
        if(!isInteger(args[0])){
            System.err.println("Port number is not a interger");
            System.exit(-1);
        }else{
            port = Integer.parseInt(args[0]);
        }
        
        ServerSocket serverSocket = new ServerSocket(port);
        System.out.println("Started server on port " + port);

        //create socket to discovery server
        Socket discoverSocket = null;
        // try{
        //     discoverSocket = new Socket(discoverIp, discoverPort);
        //     System.out.println("Connected with discover server");
        // }catch(IOException e){
        //     System.err.println("Could not connect to discovery server.");
        //     System.exit(-1);
        // }
        
        System.out.println(discoverSocket.getLocalSocketAddress());
        PrintWriter out = new PrintWriter(discoverSocket.getOutputStream(),true);
        
        //out.println("set this server");
        discoverSocket.close();
        
        // wait for connections, and process
        
        try {
            while(true) {
                // a "blocking" call which waits until a connection is requested
                Socket clientSocket = serverSocket.accept();
                System.out.println("\nAccepted connection from client");
                process(clientSocket);
            }

        }catch (IOException e) {
            System.err.println("Connection with client failed");
        }
        System.exit(0);
    }
}







