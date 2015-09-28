// CS 6421 - Simple Message Board Client in Java
// Yifei Shen G49720084
// Compile with: javac MsgClient
// Run with:     java MsgClient

import java.io.IOException;
import java.io.PrintWriter;
import java.net.Socket;
import java.net.UnknownHostException;

public class MsgClient {
    
    //MsgClient class require four arguments
    MsgClient(String host, int portnum,String name, String msg) throws IOException{
            Socket socket = null;
            //make the connection
            try{
                socket = new Socket(host, portnum);
            }catch(IOException e){
                System.err.println("Could not connect to port: 5555.");
                System.exit(-1);
            }
            
            //send the message
            PrintWriter out = new PrintWriter(socket.getOutputStream(),true);
            out.println(name);
            out.println(msg);
            socket.close();
            System.out.println("Done!");
        }
    
    public static void main(String[] args) {
        //check the number of the arguments
        if(args.length!= 3){
            System.err.println("The number of arguments is invalid");
        }else{
            //create the instance
            try{
                String host = args[0];
                int portnum = 5555;
                new MsgClient(host,portnum,args[1],args[2]);
            }catch(IOException e){
                System.err.println("Could not recognize valid host address");
            }
        }
    }
}
