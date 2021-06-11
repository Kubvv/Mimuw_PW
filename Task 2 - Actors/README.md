<div role="main"><span id="maincontent"></span><h2>Duże zadanie zaliczeniowe ze współbieżności w języku C</h2><div id="intro" class="box py-3 generalbox boxaligncenter"><div class="no-overflow"><span class="filter_mathjaxloader_equation"><span class="nolink"><h2>Wprowadzenie</h2>

<p>Zadanie będzie polegało na zaimplementowaniu pewnej wersji wzorca aktorów. Aktory to mechanizm przetwarzania pozwalający na wielowątkowe wykonywanie zestawu zadań w ramach jednego programu. Aktor to byt, który przyjmuje komunikaty z pewnego zestawu. Przyjęcie komunikatu danego rodzaju wiąże się z wykonaniem pewnego obliczenia imperatywnego, które może mieć efekty uboczne, np. polegające na przekształceniu jakiejś globalnej struktury danych, ale też na utworzeniu nowych aktorów i wysłaniu do jakichś istiejących aktorów komunikatów. Komunikaty wysyłane w tym systemie mają charakter asynchroniczny.
W obecnym zadaniu praca aktorów jest wykonywana przez wątki robocze (ang. <em>worker threads</em>) z określonej puli.</p>

<p>W niektórych realizacjach wzorca aktorów każdego aktora obsługuje osobny wątek. Tutaj tego nie robimy, gdyż przewidujemy, że liczba aktorów może być znaczna, a to prowadziłoby do dużej straty czasu na przełączanie wątków.</p>

<h2>Polecenie</h2>

<ul>
<li><p>Zaimplementuj pulę wątków zgodnie z poniższym opisem szczegółowym (3 pkt).</p></li>
<li><p>Zaimplementuj mechanizm <em>aktorów</em> zgodnie z poniższym opisem szczegółowym (3 pkt).</p></li>
<li><p>Napisz program przykładowy <strong>macierz</strong>, obliczający za pomocą aktorów sumy wierszy z zadanej tablicy (1 pkt).</p></li>
<li><p>Napisz program przykładowy <strong>silnia</strong>, obliczający za pomocą mechanizmu <em>aktorów</em> silnię zadanej liczby (1 pkt).</p></li>
<li><p>Zadbaj, aby kod był napisany w sposób klarowny i rzetelny zgodnie z poniższymi wytycznymi, w szczególności powinien sprzyjać szybkiej obsłudze danych w pamięci cache. (2 pkt).</p></li>
</ul>

<h3>Szczegółowy opis puli wątków</h3>

<p>Pulę wątków należy zaimplementować jako sposób wewnętrznej organizacji systemu aktorów. Pula ma być tworzona wraz z utworzeniem systemu wątków w opisanej poniżej procedurze <code>actor_system_create</code>. Powinna ona dysponować liczbą wątków zapisaną w stałej <code>POOL_SIZE</code>. Wątki powinny zostać zakończone automatycznie, gdy wszystkie aktory w systemie skończą działanie.</p>

<h3>Szczegółowy opis mechanizmu obliczeń <em>aktorów</em></h3>

<p>Przy pomocy puli wątków należy zaimplenentować asynchroniczne obliczenia <code>aktorów</code> jako realizację interfejsu przedstawionego w dołączonym do treści tego zadania pliku  <code>"cacti.h"</code>. Zamieszczone są tam m.in. następujące deklaracje:</p>

<pre><code class="C">typedef long message_type_t;

#define MSG_SPAWN (message_type_t)0x06057A6E
#define MSG_GODIE (message_type_t)0x60BEDEAD
#define MSG_HELLO (message_type_t)0x0

#define CAST_LIMIT 1048576

#define POOL_SIZE 3


typedef struct message
{
    message_type_t message_type;
    size_t nbytes;
    void *data;
} message_t;

typedef long actor_id_t;

typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);

typedef struct role
{
    size_t nprompts;
    act_t *prompts;
} role_t;

int actor_system_create(actor_id_t *actor, role_t *const role);

void actor_system_join(actor_id_t actor);

int send_message(actor_id_t actor, message_t message);

actor_id_t actor_id_self();
</code></pre>

<p>Wywołanie <code>int ret = actor_system_create(actor, role)</code> tworzy pierwszego aktora systemu aktorów, odpowiedzialnego za rozpoczęcie i zakończenie przetwarzania, oraz sam system aktorów obsługiwany przez pulę wątków o rozmiarze <code>POOL_SIZE</code>. Wynik tego wywołania jest 0, gdy wykonanie zakończy się prawidłowo, zaś jest ujemny w przeciwnym wypadku. W wyniku wywołania pod wskaźnikiem <code>actor</code> zapisywany jest identyfikator do pierwszego aktora systemu, który obsługuje komunikaty zgodnie z zawartością parametru <code>role</code> – dokładniejszy opis ról znajduje się poniżej. W ramach implementacji systemu utrzymywane są struktury danych opisujące stan wewnętrzny aktorów potrzebny do ich prawidłowego wykonywania. W szczególności z każdym aktorem związana jest kolejka komunikatów do niego skierowanych. W danym systemie może funkcjonować co najwyżej <code>CAST_LIMIT</code> aktorów. Można przyjąć założenie, że w danym momencie funkcjonuje jeden system aktorów. Natomiast po zakończeniu działania jednego systemu powinno być możliwe utworzenie następnego.</p>

<p>Wywołanie <code>actor_system_join(someact)</code> powoduje oczekiwanie na zakończenie działania systemu aktorów, do którego należy aktor <code>someact</code>. Po tym wywołaniu powinno być możliwe poprawne stworzenie nowego pierwszego aktora za pomocą <code>actor_system_create</code>. W szczególności taka sekwencja nie powinna prowadzić do wycieku pamięci.</p>

<p>Aktorom w systemie aktorów można wysyłać komunikaty. Do tego służy wywołanie <code>int res = send_message(someactor, msg)</code>, które wysyła komunikat opisany przez <code>msg</code> do aktora o identyfikatorze <code>someactor</code>. Wynik tego wywołania jest 0, jeśli operacja zakończy się poprawnie, -1, jeśli aktor, do którego wysyłamy komunikat, nie przyjmuje komunikatów (zob. opis MSG_GODIE poniżej), -2 jeśli aktora o podanym identyfikatorze nie ma w systemie. Wysłanie komunikatu wiąże się z włożeniem go do związanej ze wskazanym aktorem kolejki komunikatów. Kolejki mają stałe górne ograniczenie na swoją długość (nie mniejszą niż 1024 pozycje). Aktor działa, wyciągając komunikaty ze swojej kolejki i obsługując je zgodnie z przypisaną sobie rolą. Obsługa wyciągania komunikatów z kolejki powinna być tak zorganizowana, aby nie powodować zagłodzenia aktorów. Rola jest opisana przez zestaw wywołań umieszczonych w tablicy rozdzielczej, do której wskaźnik znajduje się w polu <code>prompts</code> struktury <code>role</code>.  W tablicy rozdzielczej znajdują się wskaźniki do funkcji obsługujących komunikaty różnych typów. Typy komunikatów są tożsame z indeksami w tablicy rozdzielczej. Liczba pozycji w tablicy rozdzielczej jest zapisana w polu <code>nprompts</code> struktury <code>role</code>. Funkcje obsługujące komunikaty są typu</p>

<pre><code class="C">typedef void (*const act_t)(void **stateptr, size_t nbytes, void *data);
</code></pre>

<p>Pierwszy argument zawiera wskaźnik do stanu wewnętrznego aktora, który obsługuje komunikat. Stan ten jest zależny od implementacji konkretnego obliczenia - aktory w różnych systemach lub o różnych rolach mogą korzystać z różnych formatów tego stanu. Drugi argument to rozmiar w bajtach danych wskazywanych przez trzeci argument. Wreszcie trzeci argument to wskaźnik do fragmentu globalnego stanu, który może być odczytywany i modyfikowany przez funkcję obsługi.</p>

<p>Komunikaty mają format opisany w strukturze <code>message</code>. Pole <code>message_type</code> identyfikuje typ komunikatu, pole <code>nbytes</code> określa w bajtach, jakiej długości jest trzecie pole, wreszcie trzecie pole <code>data</code> to dane, które mają format rozumiany przez funkcję obsługującą komunikaty typu wskazywanego w pierwszym argumencie.</p>

<p>W treści takich procedur można używać funkcji <code>actor_id_self()</code> do określania identyfikatora aktora, do którego wysłany jest dany komunikat.</p>

<p>Każdy aktor obsługuje trzy predefiniowane typy komunikatów</p>

<ul>
<li><code>MSG_SPAWN</code></li>
<li><code>MSG_GODIE</code></li>
<li><code>MSG_HELLO</code></li>
</ul>

<p>Wartości typów dwóch pierwszych są tak duże, że nie będą się pojawiać w implementacjach, a ich obsługa jest predefiniowana i nie można jej zmienić. Trzeci komunikat <code>MSG_HELLO</code> nie ma predefiniowanej obsługi. Jednak każdy aktor powinien obsługiwać komunikaty tego typu przez znajdującą się pod indeksem 0 funkcję w tablicy rozdzielczej roli aktora.</p>

<h4>Obsługa <code>MSG_SPAWN</code></h4>

<p>Obsługa tego komunikatu używa pola <code>data</code> komunikatu jako struktury typu <code>role</code>. Tworzy ona nowego aktora z tak przekazaną rolą, a następnie do nowo utworzonego aktora wysyła komunikat <code>MSG_HELLO</code> z polem danych zawierającym uchwyt do aktora (<code>(void*)actor_id</code>, gdzie <code>actor_id</code> jest typu <code>actor_id_t</code>), w którym następuje stworzenie nowego aktora.</p>

<h4>Obsługa <code>MSG_GODIE</code></h4>

<p>Obsługa tego komunikatu nie zagląda do żadnego pola komunikatu, ale powoduje przejście aktora, do którego wysłany jest ten komunikat, do stanu martwego, czyli stanu, w którym nie przyjmuje on już żadnych komunikatów. Jednak aktor w tym stanie wciąż obsługuje komunikaty, które zostały do niego wysłane wcześniej. Zwykle wysłanie komunikatu <code>MSG_GODIE</code> musi być poprzedzone wysłaniem komunikatu przeprowadzającego zwalnianie zasobów, którymi gospodaruje aktor (zaalokowana pamięć, zajęte deskryptory plików itp.).  Odpowiedzialnością implementujących obsługę komunikatów jest zapewnienie poprawnego działania obsługi komunikatów, które docierają do aktora po komunikacie <code>MSG_GODIE</code>.</p>

<h4>Obsługa <code>MSG_HELLO</code></h4>

<p>Obsługa tego komunikatu nie jest predefiniowana. Jednak obsługa tych komunikatów jest o tyle ważna, że pozwala ona nowemu aktorowi zdobyć identyfikator pewnego aktora w systemie, aby móc do niego wysyłać komunikaty. Tym początkowo znanym nowemu aktorowi aktorem jest aktor, który go stworzył. Identyfikator tego aktora znajduje się w argumencie <code>data</code> funkcji obsługującej takie komunikaty. Obsługa komunikatu <code>MSG_HELLO</code> w argumencie <code>stateptr</code> otrzymuje wskaźnik do miejsca w pamięci zawierającego wartość <code>NULL</code>. Może ona tę wartość zastąpić wskaźnikiem do zainicjalizowanego przez siebie stanu wewnętrznego aktora.</p>

<p>Samo prawidłowe działanie systemu może wymagać, aby w roli aktora tworzącego była obsługa komunikatów pozwalajacych na dowiedzenie się o wielu innych aktorach, jacy znajdują się w systemie. Jednak konkretne rozwiązania muszą być zależne od rodzaju wykonywanych przez system obliczeń.</p>

<h3>Obsługa sygnałów</h3>

<p>Programy, w których aktywnie działa jakaś pula wątków, powinny mieć automatycznie ustawioną obsługę sygnałów. Ta obsługa powinna zapewniać, że program po otrzymaniu sygnału (SIGINT) zablokuje możliwość dodawania nowych aktorów do działających systemów aktorów oraz przyjmowania komunikatów, dokończy obsługę wszystkich komunikatów zleconych dotąd działającym aktorom, a następnie zniszczy działający system aktorów.</p>

<p>Dla ułatwienia implementacji można założyć, że zaimplementowana biblioteka będzie testowana w taki sposób, iż wątki nie będą ginęły w testach.</p>

<h3>Opis programu <strong>macierz</strong></h3>

<p>Program <strong>macierz</strong> ma ze standardowego wejścia wczytywać dwie liczby k oraz n, każda w osobnym wierszu. Liczby te oznaczają odpowiednio liczbę wierszy oraz kolumn macierzy. Następnie program ma wczytać k*n linijek z danymi, z których każda zawiera dwie, oddzielone spacją liczby: v, t. Liczba v umieszczona w linijce i (numerację linijek zaczynamy od 0) określa wartość macierzy z wiersza floor(i/n) (numerację kolumn i wierszy zaczynamy od 0) oraz kolumny i mod n. Liczba t to liczba milisekund, jakie są potrzebne do obliczenia wartości v. Oto przykładowe poprawne dane wejściowe:</p>

<pre><code class="bash">2
3
1 2
1 5
12 4
23 9
3 11
7 2
</code></pre>

<p>Takie dane wejściowe tworzą macierz od dwóch wierszach i trzech kolumnach:</p>

<pre><code class="bash">|  1  1 12 |
| 23  3  7 |
</code></pre>

<p>Program ma za zadanie wczytać tak sformatowane wejście (można zakładać, że podawane będą tylko poprawne dane), a następnie za pomocą systemu aktorów z liczbą aktorów równą liczbie kolumn policzyć sumy wierszy. Każdy aktor będzie opiekował się obliczaniem wartości komórek z przypisanej mu kolumny. Po otrzymaniu komunikatu z numerem wiersza oraz dotychczas obliczoną sumą tego wiersza powinien odczekać liczbę milisekund, które zostały wczytane jako potrzebne do obliczenia wartości w komórce wyznaczonej przez numer wiersza z komunikatu i numer kolumny, którą opiekuje się aktor  (np. zadanie obliczeniowe wyliczenia wartości 3 z macierzy powyżej powinno odczekiwać 11 milisekund). Następnie zaś wyliczyć nową sumę przez dodanie wartości z tej komórki do dotychczas obliczonej sumy, a potem przesłać do aktora opiekującego się następną kolumną komunikat z otrzymanym numerem wiersza i nową sumą. Po obliczeniu należy wypisać sumy kolejnych wierszy na standardowe wyjście, po jednej sumie w wierszu. Dla przykładowej macierzy powyżej umieszczonej w pliku <code>data1.dat</code> wywołanie:</p>

<pre><code class="bash">$ cat data1.dat | ./macierz
</code></pre>

<p>powinno spowodować pojawienie się na wyjściu</p>

<pre><code>14
33
</code></pre>

<h3>Opis programu <strong>silnia</strong></h3>

<p>Program <strong>silnia</strong> powinien wczytywać ze standardowego wejścia pojedynczą liczbę n, a następnie obliczać za pomocą systemu aktorów liczbę n!. Każdy aktor ma otrzymywać w komunikacie dotychczas obliczoną częściową silnię k! wraz z liczbą k, tworzyć nowego aktora i wysyłać do niego (k+1)! oraz k+1. Po końcowego n! wynik powinien zostać wypisany na standardowe wyjście.  Dla przykładu wywołanie:</p>

<pre><code class="bash">$ echo 5 | ./silnia
</code></pre>

<p>powinno spowodować pojawienie się na wyjściu</p>

<pre><code>120
</code></pre>

<h2>Wymagania techniczne</h2>

<p>Do synchronizacji można korzystać tylko z mechanizmów biblioteki <code>pthreads</code>.
Można korzystać z plików nagłówkowych:</p>

<pre><code>#include &lt;pthread.h&gt;
#include &lt;semaphore.h&gt;
#include &lt;stddef.h&gt;
#include &lt;stdbool.h&gt;
#include &lt;stdint.h&gt;
#include &lt;stdio.h&gt;
#include &lt;stdlib.h&gt;
#include &lt;string.h&gt;
#include &lt;signal.h&gt;
#include &lt;errno.h&gt;
#include &lt;unistd.h&gt;
</code></pre>

<p>Powyższa lista może ulec rozszerzeniu, jeśli okaże się to konieczne.
Można założyć, że kod będzie kompilowany i testowany na serwerze <code>students</code>, co nie oznacza, że błędy znalezione na innych systemach nie wpłyną na ocenę.</p>

<p>Jako rozwiązanie należy wysłać na moodla plik <code>ab123456.tar.gz</code>, gdzie <code>ab123456</code> to login na students.
W archiwum powinien znaleźć się jeden katalog o nazwie <code>ab123456</code> (login na students) z wszystkimi plikami rozwiązania.
Kod programów przykładowych należy umieścić w plikach <code>macierz.c</code> i <code>silnia.c</code>. Nie powinna być konieczna modyfikacja plików <code>CMakeLists.txt</code>.
Ciąg poleceń:</p>

<pre><code>tar -xf ab123456.tar.gz
mkdir build &amp;&amp; cd build
cmake ../ab123456
make
</code></pre>

<p>Powinien skompilować bibliotekę do pliku <code>build/libcacti.a</code>, kompilator nie powinien wypisywać żadnych ostrzeżeń.
Rozwiązania nie wpisujące się w ten protokół będa odsyłane do poprawy. Za każde odesłanie zostaje odjęte 0,5 punkta.</p>

<p>Następnie można przetestować swoje rozwiązanie:</p>

<pre><code>make test
</code></pre>

<p>W czasie oceniania zawartość katalogu <code>test</code> zostanie podmieniona.</p>

<p>Do zadania dołączamy szablon rozwiązania zawierający m.in. plik nagłówkowy <code>cacti.h</code> z deklaracjami interfejsu programistycznego, jaki został opisany w tym zadaniu.</p>

</span></span></div>
