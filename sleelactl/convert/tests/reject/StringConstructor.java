// expect: string-static
// String is a keyword type in SLeeLa and declares no constructor that links, so new String(chars,
// offset, count) cannot be written. It used to be emitted unchanged and failed when linked.
public class StringConstructor {
    public static void main(String[] args) {
        char[] c = {'a', 'b', 'c'};
        String s = new String(c, 1, 2);
        System.out.println(s);
    }
}
