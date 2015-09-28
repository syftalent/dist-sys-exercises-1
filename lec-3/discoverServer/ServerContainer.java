import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ServerContainer {
	private Map<String, Server> objServerContainer;
	private Map<String, Server> ipportServerContainer;

	ServerobjServerContainer() {
		objServerContainer = new HashMap<String, Server>();
		ipportServerContainer = new HashMap<String, Server>();
	}

	public boolean containsServer(ConObject obj1, ConObject obj2) {
		return objServerContainer.containsKey(obj1.name + " " + obj2.name);
	}
	
	public Server getServer(ConObject obj1, ConObject obj2){
	    if(containsServer(obj1,obj2)){
	        List li = objServerContainer.get(obj1.name + " " + obj2.name);
	        return li.get(0);
	    }else{
	        return null;
	    }
	}
	
	public boolean addServer(Server server){
	    
	}
	
	public void removeServer(String ip, int port){
	    
	}


}