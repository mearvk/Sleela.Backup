// expect: library-stub
// Objects.compare(T, T, Comparator) is declared in the lib/java/jdk-28 Objects unit only as a
// signature stub that returns 0. The bare call through the static import was emitted unchecked.
// (It used Objects.requireNonNull(Object) until that member was implemented on 2026-11-21.)
import static java.util.Objects.compare;
import java.util.Comparator;

public class StaticImportStub {
    static int order(Comparator<String> byLength) {
        return compare("a", "bb", byLength);
    }

    public static void main(String[] args) {
        System.out.println(order(String.CASE_INSENSITIVE_ORDER));
    }
}
