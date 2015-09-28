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
}