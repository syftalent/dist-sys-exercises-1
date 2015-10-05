import java.util.HashSet;
import java.util.Set;

public class ConObject{
    public String name;
    private Set<ConObject> connectedObj;
    
    ConObject(String name){
        this.name = name;
        connectedObj = new HashSet<ConObject>();
    }
    
    public Set<ConObject> getConnectedObjs(){
        return connectedObj;
    }
    
    public boolean isConnected(ConObject obj){
        return connectedObj.contains(obj);
    }
    
    public void addConnectedObj(ConObject obj){
        if(connectedObj.contains(obj)){
            return;
        }else{
            connectedObj.add(obj);
        }
    }
    
    public boolean removeConnectedObj(ConObject obj){
        if(connectedObj.contains(obj)){
            connectedObj.remove(obj);
            return true;
        }else{
            return false;
        }
    }
}