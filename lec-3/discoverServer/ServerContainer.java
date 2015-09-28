import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;


public class ServerContainer {
	private Map<String, Set<Server>> objServerContainer;
	private Map<String, Server> ipportServerContainer;

	ServerContainer() {
		objServerContainer = new HashMap<String, Set<Server>>();
		ipportServerContainer = new HashMap<String, Server>();
	}

	public boolean containsServer(ConObject obj1, ConObject obj2) {
		return objServerContainer.containsKey(obj1.name + " " + obj2.name);
	}
	
	public Server getOneServer(ConObject obj1, ConObject obj2){
	    if(containsServer(obj1,obj2)){
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
	
	public Set<Server> getAllServer(ConObject obj1, ConObject obj2){
	    if(containsServer(obj1,obj2)){
	        return objServerContainer.get(obj1.name + " " + obj2.name);
	    }else{
	        return null;
	    }
	}
	
	public void addServer(Server server){
	  	String obj = server.obj1.name + " " + server.obj2.name;
	    if(containsServer(server.obj1, server.obj2)){
	        Set hs = getAllServer(server.obj1,server.obj2);
	        hs.add(server);
	    }else{
	        Set <Server>hs = new HashSet<Server>();
	        hs.add(server);
	        objServerContainer.put(obj,hs);
	    }
	    
	    String ipport = server.ip + ":" + server.port;
	    ipportServerContainer.put(ipport, server);
	}
	
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
	        }
	        return true;
	    }else{
	        return false;
	    }
	}
}