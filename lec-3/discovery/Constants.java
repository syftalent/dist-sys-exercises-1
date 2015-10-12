public final class Constants{
    private Constants() {}
    
    public static String getInfoString(String errCode){
        switch(errCode){
            //Shared by all servers
            case "ERR001":
                return "Failure: The number of parameter does not meet requirement.";
            case "ERR004":
                return "Failure: Error reading message.";
            //Sent by conversion server.
            case "ERR002":
                return "Failure: The converted value is not int or double.";
            case "ERR003":
                return "Failure: Conversion units are not recognized.";
            case "ERR005":
                return "Failure: Conversion units are not recognized.";
            //Sent by Discovery server.
            case "ERR006":
                return "Failure: Port num should be interger.";
            case "ERR007":
                return "Failure: Add Server Failed.";
            case "ERR008":
                return "Failure: Unit not existed.";
            case "ERR009":
                return "Failure: Server not found.";
            case "ERR010":
                return "Failure: Port num should be interger.";
            case "ERR011":
                return "Failure: Remove failed.";
            case "ERR012":
                return "Failure: Path not existed.";
            case "ERR013":
                return "Failure: Command not recognized.";
            
            //****************SUCCESS***************
            case "SUC":
                return "Success."
            default:
                return null;
        }
    }
}