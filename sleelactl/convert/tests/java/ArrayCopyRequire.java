// System.arraycopy and Objects.requireNonNull(Object), implemented in lib/java/jdk-28 (they were
// signature stubs): primitive, String and Object arrays, an overlapping copy within one array,
// a range error, and the null checks.
import java.util.Objects;

public class ArrayCopyRequire {
    public static void main(String[] args) {
        int[] a = {1, 2, 3, 4, 5};
        int[] b = new int[5];
        System.arraycopy(a, 1, b, 0, 3);
        System.out.println(b[0] + " " + b[2] + " " + b[3]);
        System.arraycopy(a, 0, a, 1, 4);
        System.out.println(a[0] + " " + a[1] + " " + a[4]);
        System.arraycopy(a, 2, a, 0, 3);
        System.out.println(a[0] + " " + a[2]);
        String[] s = {"x", "y", "z"};
        String[] t = new String[3];
        System.arraycopy(s, 0, t, 1, 2);
        System.out.println(t[0] + " " + t[1] + " " + t[2]);
        long[] l = {7L, 8L};
        long[] m = new long[2];
        System.arraycopy(l, 0, m, 0, 2);
        double[] d = {1.5, 2.5};
        double[] e = new double[2];
        System.arraycopy(d, 1, e, 0, 1);
        char[] c = {'h', 'i'};
        char[] k = new char[2];
        System.arraycopy(c, 0, k, 0, 2);
        System.out.println(m[1] + " " + e[0] + " " + k[1]);
        try {
            System.arraycopy(a, 4, b, 0, 3);
            System.out.println("no exception");
        } catch (ArrayIndexOutOfBoundsException ex) {
            System.out.println("out of bounds");
        }
        String v = Objects.requireNonNull("kept");
        System.out.println(v);
        try {
            Objects.requireNonNull(null);
            System.out.println("no exception");
        } catch (NullPointerException ex) {
            System.out.println("npe");
        }
    }
}
