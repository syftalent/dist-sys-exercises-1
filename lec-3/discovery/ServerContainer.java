import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.util.*;
import java.util.regex.Pattern;


public class ServerContainer implements java.io.Serializable{
	private Map<String, Set<Server>> objServerContainer;
	public Map<String, Server> ipportServerContainer;

	ServerContainer() {
		objServerContainer = new HashMap<String, Set<Server>>();
		ipportServerContainer = new HashMap<String, Server>();
	}
	
	//get all the Server table
	public Map<String, Set<Server>> getServerTable(){
	    Map<String, Set<Server>> temp = objServerContainer;
	    return temp;
	}
	
    // check if there are servers in server table that have this conversion
	public boolean containsServer(ConObject obj1, ConObject obj2) {
	    if(obj1.name.compareTo(obj2.name) > 0){
	        ConObject temp = obj1;
	        obj1 = obj2;
	        obj2 = temp;
	    }
		return objServerContainer.containsKey(obj1.name + " " + obj2.name);
	}
	
	// get only one server from server table based on two units
	public Server getOneServer(ConObject obj1, ConObject obj2){
	    if(obj1.name.compareTo(obj2.name) > 0){
	        ConObject temp = obj1;
	        obj1 = obj2;
	        obj2 = temp;
	    }
	    System.out.println("get "+obj1.name + " " + obj2.name);
	    if(containsServer(obj1,obj2)){
	        System.out.println("Contains obj12");
	        Set <Server> hs = objServerContainer.get(obj1.name + " " + obj2.name);
	        Iterator <Server> iter = hs.iterator();
	        if(iter.hasNext()){
	        	return iter.next();
	        }else{
	        	System.err.println("ERROR! No Server in it");
	        }
	    }
	    return null;
	}
	
	// return all the server in a list who can convers between these two units
	public Set<Server> getAllServerByUnits(ConObject obj1, ConObject obj2){
	    if(obj1.name.compareTo(obj2.name) > 0){
	        ConObject temp = obj1;
	        obj1 = obj2;
	        obj2 = temp;
	    }
	    if(containsServer(obj1,obj2)){
	        return objServerContainer.get(obj1.name + " " + obj2.name);
	    }else{
	        return null;
	    }
	}
	
	public Iterator<Map.Entry<String, Set<Server>>> getAllServers(){
	    return objServerContainer.entrySet().iterator();
	}
	
	/* function to add the server 
	if this server already in the server table, return false;
	if successfully added, return true */
	public boolean addServer(Server server){
	    if(server.obj1.name.compareTo(server.obj2.name) > 0){
	        ConObject temp = server.obj1;
	        server.obj1 = server.obj2;
	        server.obj2 = temp;
	    }
	    System.out.println("add "+server.obj1.name + " " + server.obj2.name);
	    String ipport = server.ip + ":" + server.port;
	    if(ipportServerContainer.containsKey(ipport)){
	        return false;
	    }
	    
	 	ipportServerContainer.put(ipport, server);

	  	String obj = server.obj1.name + " " + server.obj2.name;
	    if(containsServer(server.obj1, server.obj2)){
	        Set<Server> hs = getAllServerByUnits(server.obj1,server.obj2);
	        hs.add(server);
	    }else{
	        Set <Server>hs = new LinkedHashSet<Server>();
	        hs.add(server);
	        objServerContainer.put(obj,hs);
	    }
	    server.obj1.addConnectedObj(server.obj2);
        server.obj2.addConnectedObj(server.obj1);
	    return true;
	}
	
	/*function to remove a server from server table
	if there is no matching server in the server table, return false
	if successfully removed, return true*/
	public boolean removeServer(String ip, int port){
	    Server server;
	    String ipport = ip + ":" + port;
	    if(ipportServerContainer.containsKey(ipport)){
	        server = ipportServerContainer.get(ipport);
	        ipportServerContainer.remove(ipport);
	        String obj = server.obj1.name + " " + server.obj2.name;
	        Set<Server> hs = objServerContainer.get(obj);
	        hs.remove(server);
	        if(hs.size() == 0){
	            objServerContainer.remove(obj);
	            server.obj1.removeConnectedObj(server.obj2);
	            server.obj2.removeConnectedObj(server.obj1);
	        }
	        return true;
	    }else{
	        return false;
	    }
	}
}