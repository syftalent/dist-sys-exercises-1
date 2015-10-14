public final class Constants{
    private Constants() {}
    
    public static String getErrInfoString(String errCode){
        switch(errCode){
            //Sent by conversion server.
            case "ERR001":
                return "The number of parameter does not meet requirement.";
            case "ERR002":
                return "The converted value is not int or double.";
            case "ERR003":
                return "Conversion units are not recognized.";
            case "ERR008":
                return "Failure: Unit not existed.";
            case "ERR009":
                return "Failure: Server not found.";
            default:
                return null;
        }
    }
}