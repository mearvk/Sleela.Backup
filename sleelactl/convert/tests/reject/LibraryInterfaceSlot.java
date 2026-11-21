// expect: library-interface
// The lib/java/jdk-28 units write the interface List as a SLeeLa class, and ArrayList does not
// extend it there, so List<String> xs = new ArrayList<>() would not type-check once linked.
import java.util.ArrayList;
import java.util.List;

public class LibraryInterfaceSlot {
    public static void main(String[] args) {
        List<String> xs = new ArrayList<>();
        xs.add("a");
        System.out.println(xs.size());
    }
}
