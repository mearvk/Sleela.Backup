package org.sleela.site;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.sleela.site.Model.Module;

/**
 * Reads what a module says about itself: its documents, its SLeeLa classes (header definition, Metadata Class
 * record, fields, methods with their comments) and a per-folder layout. Everything is taken from the files as they
 * are; nothing is inferred beyond simple text patterns, and parsing is best effort (a unit that does not match the
 * patterns still appears, with what could be read).
 */
public final class ModuleCatalog {
    /** Cap on parsed SLeeLa units per module; the rest are counted but not described. */
    public static final int MAX_UNITS = 800;
    /** Cap on described documents per module. */
    public static final int MAX_DOCS = 1500;
    private static final int MAX_DEF_CHARS = 900;
    private static final int MAX_SUMMARY_CHARS = 420;

    private ModuleCatalog() {}

    // ---------------------------------------------------------------- beans

    public static final class Member {
        private final String kind, name, signature, note;
        Member(String kind, String name, String signature, String note) { this.kind = kind; this.name = name; this.signature = signature; this.note = note; }
        public String getKind() { return kind; }
        public String getName() { return name; }
        public String getSignature() { return signature; }
        public String getNote() { return note; }
    }

    public static final class Fact {
        private final String label, value;
        Fact(String label, String value) { this.label = label; this.value = value; }
        public String getLabel() { return label; }
        public String getValue() { return value; }
    }

    public static final class ClassInfo {
        private final String name, kind, parent, unitPath, fileUrl, apiUrl, syntax, definition, family;
        private final boolean material, demo;
        private final List<Fact> facts;
        private final List<Member> fields, methods;
        private final int constants;
        ClassInfo(String name, String kind, String parent, String unitPath, String fileUrl, String apiUrl, String syntax,
                  String definition, String family, boolean material, boolean demo, List<Fact> facts,
                  List<Member> fields, List<Member> methods, int constants) {
            this.name = name; this.kind = kind; this.parent = parent; this.unitPath = unitPath; this.fileUrl = fileUrl;
            this.apiUrl = apiUrl; this.syntax = syntax; this.definition = definition; this.family = family;
            this.material = material; this.demo = demo; this.facts = facts; this.fields = fields; this.methods = methods;
            this.constants = constants;
        }
        public String getName() { return name; }
        public String getKind() { return kind; }
        public String getParent() { return parent; }
        public String getUnitPath() { return unitPath; }
        public String getFileUrl() { return fileUrl; }
        public String getApiUrl() { return apiUrl; }
        public String getSyntax() { return syntax; }
        public String getDefinition() { return definition; }
        public String getFamily() { return family; }
        public boolean isMaterial() { return material; }
        public boolean isDemo() { return demo; }
        public List<Fact> getFacts() { return facts; }
        public List<Member> getFields() { return fields; }
        public List<Member> getMethods() { return methods; }
        public int getFieldCount() { return fields.size(); }
        public int getMethodCount() { return methods.size(); }
        public int getConstantCount() { return constants; }
        public String getSearchText() { return (name + " " + kind + " " + (parent == null ? "" : parent) + " " + unitPath + " " + definition).toLowerCase(Locale.ROOT); }
    }

    public static final class DocInfo {
        private final String path, title, summary, url;
        DocInfo(String path, String title, String summary, String url) { this.path = path; this.title = title; this.summary = summary; this.url = url; }
        public String getPath() { return path; }
        public String getName() { return path.substring(path.lastIndexOf('/') + 1); }
        public String getTitle() { return title; }
        public String getSummary() { return summary; }
        public String getUrl() { return url; }
    }

    public static final class Folder {
        private final String path, label;
        private final List<DocInfo> docs = new ArrayList<>();
        private final List<ClassInfo> classes = new ArrayList<>();
        private int otherFiles;
        Folder(String path) { this.path = path; this.label = path.isEmpty() ? "(module root)" : path; }
        public String getPath() { return path; }
        public String getLabel() { return label; }
        public boolean isRoot() { return path.isEmpty(); }
        public List<DocInfo> getDocs() { return docs; }
        public List<ClassInfo> getClasses() { return classes; }
        public int getDocCount() { return docs.size(); }
        public int getClassCount() { return classes.size(); }
        public int getOtherFiles() { return otherFiles; }
        public String getSummary() {
            for (String pref : new String[] {"README.md", "CPU.md", "SPECIFICATION.md", "ARCHITECTURE.md"})
                for (DocInfo d : docs) if (d.getName().equals(pref) && !d.getSummary().isEmpty()) return d.getSummary();
            for (DocInfo d : docs) if (!d.getSummary().isEmpty()) return d.getSummary();
            for (ClassInfo c : classes) if (!c.getDefinition().isEmpty()) return c.getDefinition();
            return "";
        }
        public String getSearchText() {
            StringBuilder sb = new StringBuilder(path);
            for (DocInfo d : docs) sb.append(' ').append(d.getTitle());
            for (ClassInfo c : classes) sb.append(' ').append(c.getName());
            return sb.toString().toLowerCase(Locale.ROOT);
        }
    }

    public static final class Catalog {
        private final String description;
        private final List<Folder> folders;
        private final List<ClassInfo> demos;
        private final List<Fact> stats;
        private final List<Fact> languages;
        private final List<Fact> syntaxLevels;
        private final int classCount, unitCount, docCount, skippedUnits;
        Catalog(String description, List<Folder> folders, List<ClassInfo> demos, List<Fact> stats, List<Fact> languages,
                List<Fact> syntaxLevels, int classCount, int unitCount, int docCount, int skippedUnits) {
            this.description = description; this.folders = folders; this.demos = demos; this.stats = stats;
            this.languages = languages; this.syntaxLevels = syntaxLevels; this.classCount = classCount;
            this.unitCount = unitCount; this.docCount = docCount; this.skippedUnits = skippedUnits;
        }
        public String getDescription() { return description; }
        public List<Folder> getFolders() { return folders; }
        public List<ClassInfo> getDemos() { return demos; }
        public List<Fact> getStats() { return stats; }
        public List<Fact> getLanguages() { return languages; }
        public List<Fact> getSyntaxLevels() { return syntaxLevels; }
        public int getClassCount() { return classCount; }
        public int getUnitCount() { return unitCount; }
        public int getDocCount() { return docCount; }
        public int getSkippedUnits() { return skippedUnits; }
        public int getFolderCount() { return folders.size(); }
        public boolean isManyFolders() { return folders.size() > 1; }
    }

    // ---------------------------------------------------------------- building

    public static Catalog build(ContentIndex ix, Module m, List<String> files) {
        Map<String, Folder> folders = new TreeMap<>((a, b) -> {
            if (a.isEmpty() != b.isEmpty()) return a.isEmpty() ? -1 : 1;
            return a.compareToIgnoreCase(b);
        });
        Map<String, Integer> langs = new TreeMap<>();
        Map<String, Integer> levels = new TreeMap<>();
        List<ClassInfo> demos = new ArrayList<>();
        int units = 0, classes = 0, docs = 0, skipped = 0, fields = 0, methods = 0, materials = 0;
        String apiBase = m.isApi() ? m.getApiUrl() : null;

        for (String rel : files) {
            String dir = rel.contains("/") ? rel.substring(0, rel.lastIndexOf('/')) : "";
            Folder f = folders.computeIfAbsent(dir, Folder::new);
            String name = rel.substring(rel.lastIndexOf('/') + 1);
            String ext = ContentIndex.extOf(name);
            String lang = languageName(ContentIndex.langOf(name), ext);
            langs.merge(lang, 1, Integer::sum);
            String repoPath = m.getRelDir() + "/" + rel;
            String url = m.getUrl() + "/f/" + Routes.path(rel);
            if (ext.equals("md")) {
                if (docs >= MAX_DOCS) { f.otherFiles++; continue; }
                String text = ix.readText(repoPath);
                if (text == null) { f.otherFiles++; continue; }
                String title = Markdown.firstHeading(text);
                f.docs.add(new DocInfo(rel, title == null ? name : clean(title), firstParagraph(text), url));
                docs++;
            } else if (ext.equals("sleela")) {
                units++;
                if (units > MAX_UNITS) { skipped++; f.otherFiles++; continue; }
                String text = ix.readText(repoPath);
                if (text == null) { f.otherFiles++; continue; }
                List<ClassInfo> found = parseUnit(text, rel, repoPath, url, apiBase);
                if (found.isEmpty()) { f.otherFiles++; continue; }
                for (ClassInfo c : found) {
                    f.classes.add(c);
                    classes++;
                    fields += c.getFieldCount();
                    methods += c.getMethodCount();
                    if (c.isMaterial()) materials++;
                    if (c.isDemo()) demos.add(c);
                    if (!c.getSyntax().isEmpty()) levels.merge("#sleela " + c.getSyntax(), 1, Integer::sum);
                }
            } else {
                f.otherFiles++;
            }
        }

        String description = moduleDescription(ix, m, folders.get(""));
        List<Fact> stats = new ArrayList<>();
        stats.add(new Fact("Files", String.valueOf(files.size())));
        stats.add(new Fact("SLeeLa units", String.valueOf(units)));
        stats.add(new Fact("Classes and structs", String.valueOf(classes)));
        stats.add(new Fact("Fields", String.valueOf(fields)));
        stats.add(new Fact("Methods", String.valueOf(methods)));
        stats.add(new Fact("Documents", String.valueOf(docs)));
        stats.add(new Fact("Folders", String.valueOf(folders.size())));
        stats.add(new Fact("Runnable demos", String.valueOf(demos.size())));
        if (materials > 0) stats.add(new Fact("Material units", String.valueOf(materials)));

        List<Folder> folderList = new ArrayList<>(folders.values());
        folderList.removeIf(x -> x.docs.isEmpty() && x.classes.isEmpty() && x.otherFiles == 0);
        return new Catalog(description, Collections.unmodifiableList(folderList), Collections.unmodifiableList(demos),
                Collections.unmodifiableList(stats), facts(langs), facts(levels), classes, units, docs, skipped);
    }

    private static List<Fact> facts(Map<String, Integer> counts) {
        List<Map.Entry<String, Integer>> es = new ArrayList<>(counts.entrySet());
        es.sort((a, b) -> b.getValue() - a.getValue() != 0 ? b.getValue() - a.getValue() : a.getKey().compareTo(b.getKey()));
        List<Fact> out = new ArrayList<>();
        for (Map.Entry<String, Integer> e : es) out.add(new Fact(e.getKey(), String.valueOf(e.getValue())));
        return Collections.unmodifiableList(out);
    }

    private static String languageName(String lang, String ext) {
        switch (lang) {
            case "sleela": return ext.equals("sst") ? "SLeeLa sheet (.sst)" : "SLeeLa";
            case "c": return ext.equals("h") || ext.equals("hpp") ? "C/C++ header" : ext.equals("c") ? "C" : "C++";
            case "markdown": return "Markdown";
            case "bash": return "Shell";
            case "makefile": return "Makefile";
            default: return lang.isEmpty() ? "other" : lang.toUpperCase(Locale.ROOT).length() <= 4 ? lang.toUpperCase(Locale.ROOT) : lang;
        }
    }

    /** One paragraph that says what the module is: README, then a root document, then the facade or first class. */
    private static String moduleDescription(ContentIndex ix, Module m, Folder root) {
        if (m.isReadme()) {
            String readme = ix.readText(m.getRelDir() + "/README.md");
            if (readme != null) {
                String p = firstParagraph(readme);
                if (!p.isEmpty()) return p;
            }
        }
        if (root != null) {
            List<String> names = new ArrayList<>();
            for (DocInfo d : root.docs) names.add(d.getName());
            String best = preferredDoc(names, m.getName());
            for (DocInfo d : root.docs) if (d.getName().equals(best) && !d.getSummary().isEmpty()) return d.getSummary();
            for (DocInfo d : root.docs) if (!d.getSummary().isEmpty()) return d.getSummary();
            for (ClassInfo c : root.classes) if (c.getName().equals("SLPackage") && !c.getDefinition().isEmpty()) return c.getDefinition();
            for (ClassInfo c : root.classes) if (!c.getDefinition().isEmpty()) return c.getDefinition();
        }
        return "";
    }

    /** The document that most likely describes the module: its own name (CPU.md for cpu), then overview-style names. */
    public static String preferredDoc(List<String> names, String module) {
        String own = module.toUpperCase(Locale.ROOT).replace('-', '_') + ".md";
        String own2 = module.toUpperCase(Locale.ROOT) + ".md";
        for (String pref : new String[] {"README.md", own, own2, "OVERVIEW.md", "SPECIFICATION.md", "ARCHITECTURE.md", "DESIGN.md", "MANUAL.md"})
            for (String n : names) if (n.equalsIgnoreCase(pref)) return n;
        return names.isEmpty() ? null : names.get(0);
    }

    // ---------------------------------------------------------------- markdown summaries

    private static final Pattern MD_INLINE = Pattern.compile("!\\[[^\\]]*\\]\\([^)]*\\)|\\[([^\\]]*)\\]\\([^)]*\\)|[*_`]+");

    private static final Pattern KEY_VALUE = Pattern.compile("^[A-Za-z][\\w /().-]{0,40}:\\s+\\S.*$");

    /**
     * The first prose paragraph of a markdown text: not a heading, table, list, quote, image, fence or HTML, at least
     * eight words long, and not a block of "Key: value" lines (a front-matter record such as "Package: x / Date: y").
     */
    static String firstParagraph(String md) {
        List<List<String>> paras = new ArrayList<>();
        List<String> cur = new ArrayList<>();
        boolean fence = false;
        for (String raw : md.split("\n")) {
            String line = raw.trim();
            if (line.startsWith("```")) { fence = !fence; if (!cur.isEmpty()) { paras.add(cur); cur = new ArrayList<>(); } continue; }
            if (fence) continue;
            boolean skip = line.isEmpty() || line.startsWith("#") || line.startsWith("|") || line.startsWith("<")
                    || line.startsWith("!") || line.startsWith("->") || line.startsWith("---") || line.startsWith("===") || line.startsWith(">")
                    || line.matches("^([-*+]|\\d+\\.)\\s.*");
            if (skip) { if (!cur.isEmpty()) { paras.add(cur); cur = new ArrayList<>(); } continue; }
            cur.add(line);
        }
        if (!cur.isEmpty()) paras.add(cur);
        for (List<String> para : paras) {
            boolean allKv = true;
            for (String l : para) if (!KEY_VALUE.matcher(l.replaceAll("[*_`]", "")).matches()) { allKv = false; break; }
            if (allKv) continue;
            String text = stripInline(String.join(" ", para));
            if (text.split("\\s+").length < 8) continue;
            return shorten(text, MAX_SUMMARY_CHARS);
        }
        return "";
    }

    private static String stripInline(String s) {
        Matcher mm = MD_INLINE.matcher(s);
        StringBuilder out = new StringBuilder();
        while (mm.find()) mm.appendReplacement(out, Matcher.quoteReplacement(mm.group(1) != null ? mm.group(1) : ""));
        mm.appendTail(out);
        return clean(out.toString());
    }

    // ---------------------------------------------------------------- SLeeLa units

    private static final Pattern CLASS_DECL = Pattern.compile(
            "^\\s*(?:(?:public|private|protected|abstract|final|static|sealed)\\s+)*(class|struct|interface|enum|record)\\s+([A-Za-z_][A-Za-z0-9_]*)"
            + "(?:\\s*<[^>{]*>)?(?:\\s+extends\\s+([A-Za-z_][A-Za-z0-9_.]*))?(?:\\s+implements\\s+([A-Za-z0-9_., ]+))?");
    private static final Pattern SYNTAX = Pattern.compile("(?m)^\\s*#sleela\\s+([0-9]+(?:\\.[0-9]+)?)");
    private static final Pattern MATERIAL = Pattern.compile("(?m)^\\s*material\\s*;?\\s*$");
    private static final Pattern METHOD = Pattern.compile(
            "^((?:(?:static|final|public|private|protected|abstract|synchronized|native|conducted)\\s+)*)"
            + "([A-Za-z_][A-Za-z0-9_<>,\\[\\] ]*?)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*(\\{.*|;)?$");
    private static final Pattern FIELD = Pattern.compile(
            "^((?:(?:static|final|public|private|protected|volatile|usualmem|accuratemem|unsigned)\\s+)*)"
            + "([A-Za-z_][A-Za-z0-9_<>,\\[\\]]*(?:\\s*\\[\\])*)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(=\\s*([^;]*))?;");
    private static final Pattern META_FIELD = Pattern.compile("^\\s*(String|int)\\s+(meta[A-Za-z]+)\\s*=\\s*(\"([^\"]*)\"|(-?\\d+))\\s*;");
    private static final java.util.Set<String> META_ACCESSORS = java.util.Set.of(
            "getMetaVersion", "setMetaVersion", "getMetaId", "setMetaId", "getAuthor", "setAuthor", "getDate", "setDate",
            "getCopyright", "setCopyright", "getTrademark", "setTrademark", "getCorporation", "setCorporation", "getOrigin",
            "setOrigin", "getBirthDate", "setBirthDate", "getScienceRating", "setScienceRating", "getNationalSchedule",
            "setNationalSchedule");
    private static final java.util.Set<String> NOT_TYPES = java.util.Set.of(
            "return", "if", "else", "while", "for", "new", "print", "throw", "case", "do", "switch", "break", "continue");

    static List<ClassInfo> parseUnit(String text, String rel, String repoPath, String fileUrl, String apiBase) {
        String header = headerComment(text);
        String definition = definitionOf(header);
        String family = familyOf(header);
        Matcher sm = SYNTAX.matcher(text);
        String syntax = sm.find() ? sm.group(1) : "";
        boolean material = MATERIAL.matcher(text).find();

        String[] lines = text.split("\n", -1);
        boolean[] code = codeMask(lines);
        List<ClassInfo> out = new ArrayList<>();
        int depth = 0;
        int i = 0;
        while (i < lines.length) {
            if (!code[i]) { i++; continue; }
            String line = stripStrings(lines[i]);
            Matcher cm = depth == 0 ? CLASS_DECL.matcher(line) : null;
            if (cm != null && cm.find() && !line.trim().startsWith("<")) {
                String kind = cm.group(1), name = cm.group(2), parent = cm.group(3);
                int classDepth = depth;
                List<Member> fields = new ArrayList<>(), methods = new ArrayList<>();
                Map<String, String> meta = new LinkedHashMap<>();
                List<String> pendingNote = new ArrayList<>();
                int constants = 0;
                boolean demo = false;
                boolean opened = false;
                int j = i;
                for (; j < lines.length; j++) {
                    String raw = lines[j];
                    if (!code[j]) {
                        String c = lineComment(raw);
                        if (c != null && depth == classDepth + 1) pendingNote.add(c);
                        continue;
                    }
                    String s = stripStrings(raw);
                    int before = depth;
                    if (opened && before == classDepth + 1 && j != i) {
                        String t = raw.trim();
                        Matcher mf = META_FIELD.matcher(raw);
                        if (mf.find()) {
                            meta.put(mf.group(2), mf.group(4) != null ? mf.group(4) : mf.group(5));
                        } else {
                            String note = String.join(" ", pendingNote).trim();
                            for (String stmt : splitMembers(t)) {
                                Member mem = member(stmt, note);
                                if (mem == null) continue;
                                if (mem.getKind().equals("method")) {
                                    if (META_ACCESSORS.contains(mem.getName())) continue;
                                    if (mem.getName().equals("main")) demo = true;
                                    methods.add(mem);
                                } else {
                                    if (mem.getKind().equals("constant")) constants++;
                                    fields.add(mem);
                                }
                            }
                        }
                        pendingNote.clear();
                    }
                    for (int k = 0; k < s.length(); k++) {
                        char ch = s.charAt(k);
                        if (ch == '{') { depth++; opened = true; }
                        else if (ch == '}') depth--;
                    }
                    if (opened && depth <= classDepth) break;
                }
                String api = apiBase == null ? null : apiBase + "#" + name;
                out.add(new ClassInfo(name, kind, parent, repoPath, fileUrl, api, syntax, definition, family, material, demo,
                        metaFacts(meta), Collections.unmodifiableList(fields), Collections.unmodifiableList(methods), constants));
                i = j + 1;
                if (depth < 0) depth = 0;
                continue;
            }
            for (int k = 0; k < line.length(); k++) {
                char ch = line.charAt(k);
                if (ch == '{') depth++;
                else if (ch == '}') depth--;
            }
            if (depth < 0) depth = 0;
            i++;
        }
        return out;
    }

    /** A member line can hold several short declarations ("int a; int b;"); split on top-level semicolons and bodies. */
    private static List<String> splitMembers(String t) {
        List<String> out = new ArrayList<>();
        String s = stripStrings(t);
        int depth = 0, start = 0;
        for (int k = 0; k < s.length(); k++) {
            char ch = s.charAt(k);
            if (ch == '{') { if (depth == 0) { out.add(t.substring(start, k + 1).trim()); } depth++; }
            else if (ch == '}') { depth--; if (depth == 0) start = k + 1; }
            else if (ch == ';' && depth == 0) { out.add(t.substring(start, k + 1).trim()); start = k + 1; }
        }
        if (depth == 0 && start < t.length() && !t.substring(start).trim().isEmpty()) out.add(t.substring(start).trim());
        else if (depth > 0 && out.isEmpty()) out.add(t.trim());
        return out;
    }

    private static Member member(String stmt, String note) {
        if (stmt.isEmpty() || stmt.startsWith("<") || stmt.startsWith("@") || stmt.startsWith("//")) return null;
        Matcher mm = METHOD.matcher(stmt);
        if (mm.matches()) {
            String type = mm.group(2).trim();
            String first = type.contains(" ") ? type.substring(type.lastIndexOf(' ') + 1) : type;
            if (NOT_TYPES.contains(first) || NOT_TYPES.contains(mm.group(3))) return null;
            String sig = (mm.group(1) + type + " " + mm.group(3) + "(" + mm.group(4).trim().replaceAll("\\s+", " ") + ")").trim();
            return new Member("method", mm.group(3), sig, shorten(note, 300));
        }
        Matcher fm = FIELD.matcher(stmt);
        if (fm.matches()) {
            String mods = fm.group(1);
            if (NOT_TYPES.contains(fm.group(2))) return null;
            boolean constant = mods.contains("static") && mods.contains("final");
            String sig = (mods + fm.group(2) + " " + fm.group(3) + (fm.group(5) != null ? " = " + shorten(fm.group(5).trim(), 80) : "")).trim();
            return new Member(constant ? "constant" : "field", fm.group(3), sig, shorten(note, 300));
        }
        return null;
    }

    private static List<Fact> metaFacts(Map<String, String> meta) {
        List<Fact> out = new ArrayList<>();
        String[][] order = {
            {"metaVersion", "Version"}, {"metaAuthor", "Author"}, {"metaDate", "Date"}, {"metaOrigin", "Origin"},
            {"metaCorporation", "Corporation"}, {"metaCopyright", "Copyright"}, {"metaTrademark", "Trademark"},
            {"metaScienceRating", "Science rating"}, {"metaNationalSchedule", "National schedule"}, {"metaId", "Registry id"}};
        for (String[] o : order) {
            String v = meta.get(o[0]);
            if (v == null || v.isEmpty()) continue;
            if ((o[0].equals("metaTrademark") || o[0].equals("metaCorporation") || o[0].equals("metaNationalSchedule")) && v.equals("none")) continue;
            if (o[0].equals("metaScienceRating")) v = v + " / 10";
            out.add(new Fact(o[1], v));
        }
        return Collections.unmodifiableList(out);
    }

    /** true for each line that holds code (not a comment line or inside a block comment). */
    private static boolean[] codeMask(String[] lines) {
        boolean[] code = new boolean[lines.length];
        boolean inAngle = false, inSlash = false;
        for (int i = 0; i < lines.length; i++) {
            String t = lines[i].trim();
            if (inAngle) { if (t.contains(">>")) inAngle = false; continue; }
            if (inSlash) { if (t.contains("*/")) inSlash = false; continue; }
            if (t.startsWith("<<")) { if (!t.substring(2).contains(">>")) inAngle = true; continue; }
            if (t.startsWith("/*")) { if (!t.substring(2).contains("*/")) inSlash = true; continue; }
            if (t.isEmpty() || t.startsWith("<") || t.startsWith("//") || t.startsWith("#")) continue;
            code[i] = true;
        }
        return code;
    }

    /** Text of a single-line comment ("< ... >" or "// ..."), or null. Block comment lines return null. */
    private static String lineComment(String raw) {
        String t = raw.trim();
        if (t.startsWith("<<")) {
            int e = t.indexOf(">>");
            return e > 0 ? clean(t.substring(2, e)) : null;
        }
        if (t.startsWith("<")) {
            String body = t.substring(1);
            if (body.endsWith(">")) body = body.substring(0, body.length() - 1);
            return clean(body);
        }
        if (t.startsWith("//")) return clean(t.substring(2));
        return null;
    }

    private static String stripStrings(String s) {
        StringBuilder sb = new StringBuilder(s.length());
        boolean in = false;
        for (int k = 0; k < s.length(); k++) {
            char c = s.charAt(k);
            if (in) {
                if (c == '\\' && k + 1 < s.length()) { k++; sb.append("  "); continue; }
                if (c == '"') { in = false; sb.append('"'); continue; }
                sb.append(' ');
            } else {
                if (c == '"') in = true;
                sb.append(c);
            }
        }
        return sb.toString();
    }

    /**
     * The leading comment of the unit, without the comment markers: the first block comment (<< >> or a C-style
     * block) or run of line comments that appears before any code. Pragma lines (#sleela ...) and the material
     * marker are skipped when they come first.
     */
    private static String headerComment(String text) {
        String[] lines = text.split("\n", -1);
        StringBuilder lineRun = new StringBuilder();
        int i = 0;
        while (i < lines.length) {
            String t = lines[i].trim();
            if (t.isEmpty() || t.startsWith("#") || t.matches("^material\\s*;?$")) {
                if (lineRun.length() > 0 && t.isEmpty()) return lineRun.toString();
                i++;
                continue;
            }
            if (t.startsWith("<<") || t.startsWith("/*")) {
                if (lineRun.length() > 0) return lineRun.toString();
                String close = t.startsWith("<<") ? ">>" : "*/";
                StringBuilder sb = new StringBuilder();
                String first = t.substring(2);
                int e = first.indexOf(close);
                if (e >= 0) return first.substring(0, e);
                sb.append(first).append('\n');
                for (i = i + 1; i < lines.length; i++) {
                    String l = lines[i];
                    int k = l.indexOf(close);
                    if (k >= 0) { sb.append(l, 0, k); break; }
                    sb.append(l).append('\n');
                }
                String out = sb.toString();
                return close.equals("*/") ? out.replaceAll("(?m)^\\s*\\*", "") : out;
            }
            if (t.startsWith("//")) { lineRun.append(t.substring(2)).append('\n'); i++; continue; }
            if (t.startsWith("<")) {
                String body = t.substring(1);
                if (body.endsWith(">")) body = body.substring(0, body.length() - 1);
                lineRun.append(body).append('\n');
                i++;
                continue;
            }
            break;
        }
        return lineRun.toString();
    }

    private static final Pattern FAMILY = Pattern.compile("(?s)Family:\\s*(.*)$");

    private static String definitionOf(String header) {
        if (header == null || header.isBlank()) return "";
        List<String> keep = new ArrayList<>();
        for (String l : header.split("\n")) {
            String t = l.trim();
            if (t.matches("^lib/\\S+\\.sleela$") || t.matches("^\\S+\\.sleela$")) continue;
            if (t.equals("SLeeLa Standard Library Definition.")) continue;
            keep.add(t);
        }
        String s = String.join("\n", keep);
        Matcher fm = FAMILY.matcher(s);
        if (fm.find()) s = s.substring(0, fm.start());
        s = s.replaceFirst("^\\s*Definition:\\s*", "");
        s = s.replaceAll("\n\\s*\n", "\u0000").replaceAll("\\s+", " ").replace("\u0000", "\n\n").trim();
        return shorten(s, MAX_DEF_CHARS);
    }

    private static String familyOf(String header) {
        if (header == null) return "";
        Matcher fm = FAMILY.matcher(header);
        return fm.find() ? shorten(clean(fm.group(1)), 240) : "";
    }

    private static String clean(String s) { return s.replaceAll("\\s+", " ").trim(); }

    private static String shorten(String s, int max) {
        if (s == null) return "";
        if (s.length() <= max) return s;
        int cut = s.lastIndexOf(' ', max);
        return s.substring(0, cut > max / 2 ? cut : max) + "\u2026";
    }
}
