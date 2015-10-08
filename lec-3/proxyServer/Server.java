import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.Socket;

public class Server {
	ConObject obj1;
	ConObject obj2;
	String ip;
	int port;

	Server(String ip, int port, ConObject obj1, ConObject obj2) {
		this.obj1 = obj1;
		this.obj2 = obj2;
		this.ip = ip;
		this.port = port;
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
}