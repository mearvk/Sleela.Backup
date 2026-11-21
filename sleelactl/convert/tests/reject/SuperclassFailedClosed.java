// expect: library-type
// The superclass is neither converted in this run nor a lib/java unit, so the class header names a
// type nothing defines. The header was written unchecked and linked as "extends unknown type".
public class SuperclassFailedClosed {
    static class Child extends sun.util.resources.OpenListResourceBundle {
        protected Object[][] getContents() { return new Object[0][]; }
    }

    public static void main(String[] args) {
        System.out.println("x");
    }
}
