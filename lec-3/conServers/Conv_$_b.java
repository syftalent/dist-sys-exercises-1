/******************************************************************************
 *
 *  CS 6421 - Simple Conversation
 *  Compilation:  javac Conv_$_b.java
 *  Execution:    java Conv_$_b port
 *
 *  % java Conv_$_b portnum
 ******************************************************************************/
 
import java.net.Socket;
import java.net.ServerSocket;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.UnknownHostException;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.regex.Pattern;

public class Conv_$_b {
    final static String discoverIp = "52.23.200.76";
    final static int discoverPort = 1234;

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
            System.out.println("Error reading message");
            out.close();
            in.close();
            clientSocket.close();
        }

        System.out.println("Received message: " + userInput);
        //--TODO: add your converting functions here, msg = func(userInput);
        
        String[] tokens = userInput.split(" ");
        if(tokens.length != 3)
            out.println("Invalid Input!");
        // else if(!isInteger(tokens[2]))
        //     out.println("Invalid Input!");
        else{
            double result;
            double num = Double.parseDouble(tokens[2]);
            if (tokens[0].equals("$") && tokens[1].equals("b")){
                result = num * 2;
                out.println(result + " bananas");
            }else if (tokens[0].equals("b") && tokens[1].equals("$")){
                result = num / 2;
                out.println(result + " dollars");
            }else{
                out.println("Invalid Input!");
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

    public static void main(String[] args) throws Exception {

        //check if argument length is invalid
        if(args.length != 1) {
            System.err.println("Usage: java ConvServer port");
        }
        
        // create server socket
        int port = Integer.parseInt(args[0]);
        ServerSocket serverSocket = new ServerSocket(port);
        System.err.println("Started server on port " + port);

        //create socket to discovery server
        Socket discoverSocket;
        try{
            discoverSocket = new Socket(discoverIp, discoverPort);
        }catch(IOException e){
            System.err.println("Could not connect to discovery server.");
            System.exit(-1);
        }
        
        //Register to discovery Server
        PrintWriter out = new PrintWriter(discoverSocket.getOutputStream(),true);
        out.println("set this server");
        discoverSocket.close();

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



