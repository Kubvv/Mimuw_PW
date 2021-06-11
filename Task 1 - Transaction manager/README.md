<h6>Zadanie zaliczeniowe ze współbieżności w języku Java</h6>

<p>Transakcja to sekwencja operacji, które stanowią pewną całość i jako takie muszą być wykonane niepodzielnie i w izolacji. Niepodzielność oznacza, że albo wykonają się wszystkie operacje z sekwencji stanowiącej transakcję, albo nie wykona się żadna z nich. Izolacja natomiast sprawia, że wyniki poszczególnych operacji z sekwencji danej transakcji nie są widoczne dla operacji z innych transakcji dopóki cała sekwencja danej transakcji się nie zakończy. Zapewnianie niepodzielności i izolacji jest celem zarządcy transakcji. Twoim zadaniem będzie napisanie takiego współbieżnego zarządcy w języku Java zgodnie z poniższymi wymaganiami. Do rozwiązania zadania należy wykorzystać <a href="ab123456.zip">załączony szablon</a>.</p>
<h1 id="specyfikacja">Specyfikacja</h1>
<p>Rozważmy system, w którym działa pewna (nieznana) liczba wątków. Wątki operują na ustalonej kolekcji zasobów (obiekty klas dziedziczących po klasie <code>cp1.base.Resource</code> z załączonego szablonu), z których każdy ma stały, unikalny identyfikator (<code>cp1.base.ResourceId</code>). W wyniku takich operacji (reprezentowanych przez dowolne obiekty implementujące interfejs <code>cp1.base.ResourceOperation</code>) kolekcja zasobów się nie zmienia, za to może się zmieniać stan poszczególnych zasobów.</p>
<p>Operacje na zasobach wykonywane są w ramach transakcji. W dowolnym momencie czasu, dany wątek może wykonywać co najwyżej jedną transakcję. Transakcje są koordynowane przez menedżera transakcji (implementującego poniższy interfejs <code>cp1.base.TransactionManager</code>):</p>
<pre class="sourceCode java"><code class="sourceCode java"><span class="kw">public</span> <span class="kw">interface</span> TransactionManager {

    <span class="kw">public</span> <span class="dt">void</span> <span class="fu">startTransaction</span>(
    ) <span class="kw">throws</span>
        AnotherTransactionActiveException;

    <span class="kw">public</span> <span class="dt">void</span> <span class="fu">operateOnResourceInCurrentTransaction</span>(
            ResourceId rid,
            ResourceOperation operation
    ) <span class="kw">throws</span>
        NoActiveTransactionException,
        UnknownResourceIdException,
        ActiveTransactionAborted,
        ResourceOperationException,
        InterruptedException;

    <span class="kw">public</span> <span class="dt">void</span> <span class="fu">commitCurrentTransaction</span>(
    ) <span class="kw">throws</span>
        NoActiveTransactionException,
        ActiveTransactionAborted;

    <span class="kw">public</span> <span class="dt">void</span> <span class="fu">rollbackCurrentTransaction</span>();

    <span class="kw">public</span> <span class="dt">boolean</span> <span class="fu">isTransactionActive</span>();

    <span class="kw">public</span> <span class="dt">boolean</span> <span class="fu">isTransactionAborted</span>();
}</code></pre>
<p>Podczas tworzenia (metoda <code>newTM</code> klasy <code>cp1.solution.TransactionManagerFactory</code>) menedżer transakcji otrzymuje kolekcję zasobów, które przechodzą pod jego wyłączną kontrolę, oraz interfejs do pytania o aktualny czas, który jest potrzebny przy zarządzaniu transakcjami.</p>
<h2 id="rozpoczynanie-transakcji">Rozpoczynanie transakcji</h2>
<p>Zanim wątek wykona jakąkolwiek operację na zasobie, musi rozpocząć (aktywować) transakcję poprzez wywołanie metody <code>startTransaction</code> menedżera transakcji (MT). Rozpoczęcie transakcji nie udaje się jedynie wtedy, gdy wątek wywołujący ww. metodę nie zakończył jeszcze poprzedniej transakcji. W takim przypadku wywołanie metody <code>startTransaction</code> MT kończy się zgłoszeniem wyjątku <code>cp1.base.AnotherTransactionActiveException</code>. W efekcie wątek może zakończyć bieżącą transakcję, a następnie ponowić nieudaną próbę rozpoczęcia nowej transakcji. Jeśli natomiast wątkowi uda się rozpocząć transakcję, to transakcja ta pozostaje <em>aktywna</em> do momentu, gdy nie zostanie zakończona w sposób opisany dalej.</p>
<h2 id="wykonywanie-operacji-w-ramach-transakcji">Wykonywanie operacji w ramach transakcji</h2>
<p>Wykonanie przez wątek operacji na zasobie odbywa się poprzez wywołanie przez niego metody <code>operateOnResourceInCurrentTransaction</code> MT, której argumenty oznaczają identyfikator zasobu oraz operację do wykonania. Jeśli wątek nie ma aktywnej transakcji, wywołanie ww. metody kończy się zgłoszeniem wyjątku <code>cp1.base.NoActiveTransactionException</code>. Analogicznie, wywołanie dla identyfikatora zasobu, który nie jest pod kontrolą MT, kończy się wyjątkiem <code>cp1.base.UnknownResourceIdException</code> z argumentem odpowiadającym identyfikatorowi niekontrolowanego zasobu. Wreszcie, jeśli aktywna transakcja została wcześniej anulowana, jak wyjaśniamy dalej, to wywołanie kończy się wyjątkiem <code>cp1.base.ActiveTransactionAborted</code>.</p>
<p>W przeciwnym razie wątek rozpoczyna ubieganie się o dostęp do wskazanego zasobu. Jeśli w ramach aktywnej transakcji wątek już taki dostęp uzykał poprzednio, to jest udało mu się wcześniej co najmniej raz rozpocząć wykonanie jakiejś operacji na tym samym zasobie, to dostęp jest mu przyznawany natychmiastowo. W przeciwnym przypadku sprawdzane jest, czy inna aktywna transakcja miała kiedykolwiek przyznany dostęp do zasobu. Jeśli taka transakcja nie istnieje, to dostęp zostaje przyznany rozważanej transakcji. W przeciwnym razie z kolei rozważana transakcja musi potencjalnie zaczekać na zakończenie transakcji, która uzyskała wcześniej dostęp do wskazanego zasobu.</p>
<p>Jednakże gdy takie oczekiwanie doprowadziłoby do zakleszczenia pewnej grupy transakcji, MT <em>anuluje</em> tę z transakcji w grupie, która została rozpoczęta najpóźniej. Czas rozpoczęcia mierzony jest wartością metody <code>getTime</code> interfejsu <code>LocalTimeProvider</code> (przekazanemu MT przy jego tworzeniu), a w przypadku, gdy kilka transakcji ma ten sam czas rozpoczęcia, decydują identyfikatory wykonujących je wątków (metoda <code>getId</code> klasy <code>Thread</code>), to jest transakcja wątku z największym identyfikatorem uważana jest za najpóźniejszą spośród transakcji z tym samym czasem rozpoczęcia. Anulowanie transakcji wymaga także przerwania wątku, który ją rozpoczął (metoda <code>interrupt</code> klasy <code>Thread</code>).</p>
<p>Anulowana transakcja nie może być kontynuowana, to jest można jedynie ją zakończyć, jak wyjaśniamy dalej. W szczególności, jak wspomniano wcześniej, próba wykonania jakiejkolwiek operacji w ramach tej transakcji kończy się wyjątkiem <code>cp1.base.ActiveTransactionAborted</code>.</p>
<p>Gdy wątek uzyska już dostęp do zasobu, MT rozpoczyna wykonywanie na tym zasobie operacji przekazanej jako argument funkcji <code>operateOnResourceInCurrentTransaction</code> poprzez wywołanie na obiekcie tej operacji metody <code>execute</code> interfejsu <code>cp1.base.ResourceOperation</code> z argumentem odpowiadającym zasobowi. Wykonanie operacji musi się odbyć w kontekście wątku, który tę operację zlecił, to jest żaden wątek nie może wykonać ww. metody <code>execute</code> na obiekcie operacji przekazanej MT przez inny wątek.</p>
<p>Wykonanie operacji może zakończyć się wyjątkiem <code>cp1.base.ResourceOperationException</code>, który musi zostać przekazany przez MT do wołającego wątku. W takim przypadku mówimy, że operacja kończy się niepowodzeniem. Metoda <code>execute</code> operacji kończącej się niepowodzeniem gwarantuje, że zasób pozostaje w nienaruszonym stanie, to jest stanie sprzed wykonania tej operacji.</p>
<p>Operacja, której wykonanie nie spowoduje wyjątku <code>cp1.base.ResourceOperationException</code> kończy się natomiast sukcesem. Taka operacja może zmodyfikować stan zasobu.</p>
<p>Niezależnie od tego, jak się zakończy, dana operacja może być przez wątek wykonywana wiele razy, na tych samych lub różnych zasobach. Co więcej, różne wykonania tej samej operacji mogą się różnie kończyć.</p>
<p>Należy także zauważyć, iż w trakcie oczekiwania na zasób lub wykonywania na nim operacji, wątek może zostać przerwany (poprzez wywołanie metody <code>interrupt</code> z klasy <code>Thread</code>). W takim przypadku wywołanie metody <code>operateOnResourceInCurrentTransaction</code> MT kończy się wyjątkiem <code>InterruptedException</code>, pozostawiając bez zmian zarówno stan zasobu, jak i transakcji, przy czym w zależności od momentu, w którym pojawiło się przerwanie, transakcja może dostać lub nie dostęp do zasobu. Dokładniej, jeśli przerwanie pojawiło się przed lub w trakcie przyznawania dostępu, to transakcja tego dostępu nie uzyskuje. W przeciwnym przypadku dostęp zostaje przyznany.</p>
<h2 id="kończenie-transakcji">Kończenie transakcji</h2>
<p>Wątek może <em>zakończyć</em> aktywną transakcję na jeden z dwóch sposobów:</p>
<ul>
<li>poprzez zatwierdzenie (wywołanie metody <code>commitCurrentTransaction</code> MT) albo</li>
<li>poprzez cofnięcie (wywołanie metody <code>rollbackCurrentTransaction</code> MT).</li>
</ul>
<p>Zatwierdzenie transakcji powoduje, że wszelkie zmiany zasobów MT wykonane przez operacje w ramach tej transakcji stają się widoczne dla innych transakcji. Zatwierdzenie transakcji przez wątek nie udaje się tylko w dwóch przypadkach. Po pierwsze, gdy wątek nie ma aktywnej transakcji, zgłaszany jest wyjątek <code>cp1.base.NoActiveTransactionException</code>. Po drugie, gdy aktywna transakcja wątku została wcześniej anulowana, zgłaszany jest wyjątek <code>cp1.base.ActiveTransactionAborted</code> a transakcja nie jest kończona.</p>
<p>Cofnięcie transakcji powoduje z kolei, że wszelkie zmiany zasobów MT wykonane przez operacje w ramach tej transakcji zostają wycofane, to jest stan tych zasobów jest taki sam, jak w momencie rozpoczynania transakcji. Cofnięcie transakcji przez wątek zawsze się udaje. W szczególności, gdy wątek nie ma aktywnej transakcji, jest to po prostu operacja pusta.</p>
<p>Wycofanie zmian wykonanych przez daną operację na danym zasobie odbywa się poprzez wywołanie na obiekcie tej operacji metody <code>undo</code> interfejsu <code>cp1.base.ResourceOperation</code> z argumentem odpowiadającym zasobowi. Jak w przypadku metody <code>execute</code>, wywołanie metody <code>undo</code> dla operacji musi odbyć się w kontekście wątku, który tę operację zlecił. W przeciwieństwie do metody <code>execute</code>, metoda <code>undo</code> nie zgłasza żadnych wyjątków. Wycofywane są jedynie operacje zakończone sukcesem i robione jest to w odwrotnej kolejności niż kolejność ich wykonania w ramach transakcji.</p>
<p>Zakończenie transakcji na którykolwiek z dwóch możliwych sposobów powoduje, że MT przestaje przechowywać jakiekolwiek informacje związane z tą transakcją.</p>
<h2 id="sprawdzanie-stanu-transakcji">Sprawdzanie stanu transakcji</h2>
<p>MT udostępnia jeszcze dwie metody pozwalające wątkowi sprawdzić stan swojej transakcji. Metoda <code>isTransactionActive</code>, wywołana w kontekście wątku, zwraca <code>true</code> wtedy i tylko wtedy, gdy wątek ten ma aktywną transakcję. Metoda <code>isTransactionAborted</code> z kolei zwraca <code>true</code> wtedy i tylko wtedy, gdy wątek ma aktywną transakcję, lecz została ona anulowana.</p>
<h1 id="wymagania">Wymagania</h1>
<p>Twoim zadaniem jest zaimplementowanie MT według powyższej specyfikacji i dostarczonego szablonu przy wykorzystaniu mechanizmów współbieżności języka Java 11. Musisz zadbać o zapewnienie żywotności i bezpieczeństwa rozwiązania. Staraj się także zmaksymalizować równoległość. W szczególności operacje na różnych zasobach zlecane przez różne wątki powinny wykonywać się równolegle (oczwiście respektując ograniczenia wynikające z powyższej specyfikacji). Twój kod źródłowy powinien być napisany w zgodzie z dobrymi praktykami programistycznymi</p>
<p>Szczegółowe dodatkowe wymagania formalne są następujące.</p>
<ol style="list-style-type: decimal">
<li>Rozwiązanie musi być napisane samodzielnie.</li>
<li>Nie możesz w żaden sposób zmieniać zawartości pakietów <code>cp1.base</code> oraz <code>cp1.demo</code>.</li>
<li>Klasy implementujące rozwiązanie możesz dodawać jedynie w pakiecie <code>cp1.solution</code>, ale nie możesz tworzyć w tym pakiecie żadnych podpakietów.</li>
<li>W klasie <code>cp1.solution.TransactionManagerFactory</code> musisz dodać treść metody <code>newTM</code>, która będzie wykorzystywana do instancjonowania zaimplementowanego przez Ciebie MT. Każde wywołanie tej metody powinno tworzyć nowy obiekt MT. Wiele obiektów MT powinno być w stanie działać w tym samym czasie. Nie wolno natomiast w żaden sposób zmieniać sygnatury tej metody ani nazwy klasy czy jej lokalizacji.</li>
<li>Możesz stworzyć sobie własne pakiety do testów, np. <code>cp1.tests</code>, ale te pakiety będą ignorowane przy testowaniu przez nas, więc w szczególności kod Twojego MT nie może od nich zależeć.</li>
<li>W plikach źródłowych Javy nie możesz używać nieanglojęzycznych znaków (w szczególności polskich znaków).</li>
</ol>

