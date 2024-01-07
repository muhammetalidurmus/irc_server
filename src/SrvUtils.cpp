#include "../include/Server.hpp"

// Partial komutları işler
// İstemci soket dosya tanımlayıcısını alır ve tamamlanmamış komutları işler.
void Server::processPartialCommands(int clientSocketFD)
{
	string& clientBuffer = clientBuffers[clientSocketFD].getBuffer();
	size_t endOfCommand;
	string command;

	// Komutlar '/' ile başlıyorsa
	if (clientBuffer[0] == '/')
	{
		while ((endOfCommand = clientBuffer.find('\n')) != string::npos)
		{
			command = clientBuffer.substr(0, endOfCommand);
			clientBuffer.erase(0, endOfCommand + 1);
			// Komutu işleyen CommandParser sınıfı kullanılır.
			CommandParser::commandParser(command.c_str(), _clients[clientSocketFD], this);
		}
	}
	else
	{
		// Komutlar '\r\n' ile bitiyorsa
		while ((endOfCommand = clientBuffer.find("\r\n")) != string::npos)
		{
			command = clientBuffer.substr(0, endOfCommand);
			clientBuffer.erase(0, endOfCommand + 2);  // '\r\n' karakterlerini sil
			// Komutu işleyen CommandParser sınıfı kullanılır.
			CommandParser::commandParser(command.c_str(), _clients[clientSocketFD], this);
		}
	}
}

// İstemci isteğini işler
// İstemci soket dosya tanımlayıcısını alır ve isteği işler.
void Server::handleClient(int clientSocketFD)
{
	if (clientSocketFD == _bot->getSocket())
	{
		// Bot'un soketi dinlenir.
		_bot->listen();
	}
	else
	{
 		const size_t BUFFER_SIZE = 512;
		char tempBuffer[BUFFER_SIZE];
		memset(tempBuffer, 0, BUFFER_SIZE);

		// İstemciden gelen veriyi okur.
		ssize_t received = recv(clientSocketFD, tempBuffer, BUFFER_SIZE - 1, 0);
		if (received > 0) {
			// Gelen veriyi istemci tamponuna ekler ve kısmi komutları işler.
			clientBuffers[clientSocketFD].appendtoBuffer(string(tempBuffer, received));
			cout << "Received: " << tempBuffer << endl;
			processPartialCommands(clientSocketFD);
		} else if (received == 0 || errno == ECONNRESET) {
			// İstemci bağlantısı kapatıldıysa veya hata durumunda
			clientDisconnect(clientSocketFD);
			clientBuffers.erase(clientSocketFD);
		} else {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				// Hata durumunda
				ErrorLogger("recv error", __FILE__, __LINE__);
				close(clientSocketFD);
				clientBuffers.erase(clientSocketFD);
			}
		}
	}
}

// İstemci bağlantısını sonlandırır
// İstemci tüm kanallardan çıkarılır ve bağlantı kapatılır.
void Server::clientDisconnect(int clientSocketFD)
{
     try
    {
        // İstemci haritasında istemci aranır ve bulunursa işlemler gerçekleştirilir.
        std::map<int, Client*>::iterator it = _clients.find(clientSocketFD);
        if (it == _clients.end()) {
            write(STDOUT_FILENO, "Client not found for removal.\n", 30);
            return;
        }

        // Tüm kanallardan istemci çıkarılır ve ayrılır.
        removeClientFromAllChannels(it->second);
        it->second->leave();

        // İstemci bağlantısı kapatıldı bilgisi loglanır.
        ostringstream messageStreamDisconnect;
        messageStreamDisconnect << "Client " << it->second->getNickName() << " has disconnected.";
        log(messageStreamDisconnect.str());
#if defined(__linux__)
        epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocketFD, NULL);
#elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
        struct kevent evSet;
        EV_SET(&evSet, clientSocketFD, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(kq, &evSet, 1, NULL, 0, NULL);
#endif
     // İstemci soketi kapatılır ve bellekten temizlenir.
        close(clientSocketFD);
        delete it->second;
        _clients.erase(it);
    }
    catch (const std::exception &e)
    {
        write(STDOUT_FILENO, e.what(), strlen(e.what()));
    }
}

// Sunucu şifresini ayarlar
void Server::setSrvPass(const string& pass) {
	_serverPass = pass;
}

// Verilen şifreyi sunucu şifresiyle karşılaştırır
bool Server::verifySrvPass(const string& pass) {
	if (_serverPass == pass) {
		return true;
	}
	return false;
}
// Sinyali işler ve sunucuyu kapatır.
void Server::signalHandler(int signum)
{
	Server::getInstance()->shutdownSrv();
	exit(signum);
}
// Tüm kanalları kaldırır ve bağlantıyı kapatır.
void Server::shutdownSrv()
{
	string outmessage = "Sunucu kapatılıyor...\n";
	write(STDOUT_FILENO, outmessage.c_str(), outmessage.size());

	// Tüm istemciler için işlemler gerçekleştirilir.
	for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
		Client* client = it->second;
		if (client != NULL) {
			// İstemciye kapatılma mesajı gönderilir ve tüm kanallardan çıkarılır.
			client->sendMessage("Sunucu kapatılıyor. Bağlantınız sonlandırılıyor.");
			removeClientFromAllChannels(client);
			close(it->first);
			delete client;
		}
	}
	_clients.clear();

	// Sunucu soketi kapatılır.
	if (_serverSocketFD != -1) {
		close(_serverSocketFD);
		_serverSocketFD = -1;
	}

	// Bot nesnesi bellekten temizlenir.
	if (_bot != NULL) {
		delete _bot;
		_bot = NULL;
	}

#if defined(__linux__)
	if (epollFd != -1) {
		close(epollFd);
 	}
#elif defined(__APPLE__) || defined(__MACH__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	if (kq != -1) {
		close(kq);
	}
#endif
	// Bellekte sızıntı kontrolü yapılır.
	system("leaks ircserv");
	string outmessage2 = "Sunucu kapatıldı.\n";
	write(STDOUT_FILENO, outmessage2.c_str(), outmessage2.size());
}
