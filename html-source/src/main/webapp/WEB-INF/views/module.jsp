<t:layout title="${pageTitle}" active="modules">
<div class="crumbs"><a href="${ctx}/">Home</a><span class="sep">/</span><a href="${ctx}/modules">Modules</a><span class="sep">/</span><c:out value="${module.name}"/></div>
<div class="section-title">
  <h1><c:out value="${module.name}"/></h1>
  <div class="btn-row" style="margin:0">
    <c:if test="${module.api}"><a class="btn small" href="${ctx}${module.apiUrl}">API docs</a></c:if>
    <c:if test="${not empty counterpart}"><a class="btn small ghost" href="${ctx}${counterpart.url}"><c:out value="${counterpart.kind == 'lib' ? 'Library module' : 'System module'}"/>: <c:out value="${counterpart.name}"/></a></c:if>
  </div>
</div>
<p class="muted"><span class="pill"><c:out value="${module.kind == 'lib' ? 'library module' : 'system module'}"/></span> <code><c:out value="${module.relDir}"/></code></p>

<c:if test="${not empty catalog.description}"><p class="lede"><c:out value="${catalog.description}"/></p></c:if>

<h2>At a glance</h2>
<div class="grid glance">
  <c:forEach var="s" items="${catalog.stats}">
    <div class="card stat"><b><c:out value="${s.value}"/></b><p><c:out value="${s.label}"/></p></div>
  </c:forEach>
</div>
<div class="facts-row">
  <c:if test="${not empty catalog.languages}">
    <p><strong>Languages:</strong>
      <c:forEach var="l" items="${catalog.languages}"><span class="pill"><c:out value="${l.label}"/> <c:out value="${l.value}"/></span></c:forEach>
    </p>
  </c:if>
  <c:if test="${not empty catalog.syntaxLevels}">
    <p><strong>Syntax levels:</strong>
      <c:forEach var="l" items="${catalog.syntaxLevels}"><span class="pill syntax"><c:out value="${l.label}"/> &times; <c:out value="${l.value}"/></span></c:forEach>
    </p>
  </c:if>
  <c:if test="${catalog.skippedUnits > 0}">
    <p class="muted"><c:out value="${catalog.skippedUnits}"/> further SLeeLa units are listed under Files but not described here (the page describes the first <c:out value="${catalog.unitCount - catalog.skippedUnits}"/>).</p>
  </c:if>
</div>

<c:if test="${not empty examples}">
  <h2>Examples</h2>
  <div class="grid">
    <c:forEach var="g" items="${examples}">
      <a class="card" href="${ctx}${g.url}"><h3><c:out value="${g.label}"/></h3><p>${g.count} file(s)</p></a>
    </c:forEach>
  </div>
</c:if>

<c:if test="${not empty catalog.demos}">
  <h2>Runnable demos <span class="pill">${fn:length(catalog.demos)}</span></h2>
  <p class="muted">Units with a <code>main()</code> method. Run them with <code>sleela run &lt;file&gt;</code> from a SLeeLa checkout.</p>
  <ul class="filelist demos">
    <c:forEach var="d" items="${catalog.demos}">
      <li class="demo-item"><a href="${ctx}${d.fileUrl}"><c:out value="${d.unitPath}"/></a> <span class="muted">&mdash; <c:out value="${d.name}"/></span></li>
    </c:forEach>
  </ul>
</c:if>

<c:if test="${not empty intro}"><h2>README</h2><div class="doc">${intro}</div></c:if>

<c:if test="${not empty catalog.folders}">
  <h2>Contents <span class="pill">${catalog.classCount} classes</span><span class="pill">${catalog.docCount} documents</span><c:if test="${catalog.manyFolders}"><span class="pill">${catalog.folderCount} folders</span></c:if></h2>
  <p class="muted">Each class shows the definition from its source header, its Metadata Class record, and its fields and methods with the comment written above each one. Documents show their title and first paragraph.</p>
  <input class="filter" type="search" placeholder="Filter folders, classes, documents..." data-filter-list=".cat-item" aria-label="Filter contents">
  <c:forEach var="fo" items="${catalog.folders}">
    <details class="folder cat-item" data-name="${fn:escapeXml(fo.searchText)}" ${fo.root || !catalog.manyFolders ? 'open' : ''}>
      <summary>
        <span class="folder-name"><c:out value="${fo.label}"/></span>
        <c:if test="${fo.classCount > 0}"><span class="pill">${fo.classCount} classes</span></c:if>
        <c:if test="${fo.docCount > 0}"><span class="pill">${fo.docCount} docs</span></c:if>
        <c:if test="${fo.otherFiles > 0}"><span class="pill">${fo.otherFiles} other files</span></c:if>
        <c:if test="${not fo.root and not empty fo.summary}"><span class="folder-sum"><c:out value="${fo.summary}"/></span></c:if>
      </summary>

      <c:if test="${not empty fo.docs}">
        <h3 class="sub">Documents</h3>
        <div class="grid wide">
          <c:forEach var="d" items="${fo.docs}">
            <a class="card" href="${ctx}${d.url}"><h3><c:out value="${d.title}"/></h3><p class="muted small"><c:out value="${d.name}"/></p><c:if test="${not empty d.summary}"><p><c:out value="${d.summary}"/></p></c:if></a>
          </c:forEach>
        </div>
      </c:if>

      <c:if test="${not empty fo.classes}">
        <h3 class="sub">Classes</h3>
        <c:forEach var="k" items="${fo.classes}">
          <div class="classcard" id="${fn:escapeXml(k.name)}">
            <div class="classhead">
              <h4><c:out value="${k.name}"/></h4>
              <span class="pill"><c:out value="${k.kind}"/></span>
              <c:if test="${not empty k.parent}"><span class="pill">extends <c:out value="${k.parent}"/></span></c:if>
              <c:if test="${not empty k.syntax}"><span class="pill syntax">#sleela <c:out value="${k.syntax}"/></span></c:if>
              <c:if test="${k.material}"><span class="pill">material</span></c:if>
              <c:if test="${k.demo}"><span class="pill green">runnable</span></c:if>
              <span class="classlinks"><a href="${ctx}${k.fileUrl}">source</a><c:if test="${not empty k.apiUrl}"> &middot; <a href="${ctx}${k.apiUrl}">API</a></c:if></span>
            </div>
            <p class="muted small"><code><c:out value="${k.unitPath}"/></code></p>
            <c:if test="${not empty k.definition}"><p class="def"><c:out value="${k.definition}"/></p></c:if>
            <c:if test="${not empty k.family}"><p class="muted small"><strong>Family:</strong> <c:out value="${k.family}"/></p></c:if>
            <c:if test="${not empty k.facts}">
              <dl class="facts">
                <c:forEach var="f" items="${k.facts}"><div><dt><c:out value="${f.label}"/></dt><dd><c:out value="${f.value}"/></dd></div></c:forEach>
              </dl>
            </c:if>
            <c:if test="${k.fieldCount > 0 || k.methodCount > 0}">
              <details class="members-box">
                <summary><c:out value="${k.fieldCount}"/> fields<c:if test="${k.constantCount > 0}"> (<c:out value="${k.constantCount}"/> constants)</c:if>, <c:out value="${k.methodCount}"/> methods</summary>
                <c:if test="${k.fieldCount > 0}">
                  <div class="table-wrap"><table class="members">
                    <tr><th>Field</th><th>Declaration</th><th>Note</th></tr>
                    <c:forEach var="mem" items="${k.fields}"><tr><td><c:out value="${mem.name}"/></td><td><code><c:out value="${mem.signature}"/></code></td><td><c:out value="${mem.note}"/></td></tr></c:forEach>
                  </table></div>
                </c:if>
                <c:if test="${k.methodCount > 0}">
                  <div class="table-wrap"><table class="members">
                    <tr><th>Method</th><th>Signature</th><th>Note</th></tr>
                    <c:forEach var="mem" items="${k.methods}"><tr><td><c:out value="${mem.name}"/></td><td><code><c:out value="${mem.signature}"/></code></td><td><c:out value="${mem.note}"/></td></tr></c:forEach>
                  </table></div>
                </c:if>
                <p class="muted small">The eleven Metadata Class accessors are summarised above rather than listed.</p>
              </details>
            </c:if>
          </div>
        </c:forEach>
      </c:if>
    </details>
  </c:forEach>
</c:if>

<h2>Files <span class="pill">${fn:length(files)}</span></h2>
<c:choose>
  <c:when test="${empty files}"><p class="muted">No browsable text files.</p></c:when>
  <c:otherwise>
    <details class="folder" ${fn:length(files) gt 60 ? '' : 'open'}>
      <summary>All files in this module</summary>
      <input class="filter" type="search" placeholder="Filter files..." data-filter-list=".file-item" aria-label="Filter files">
      <ul class="filelist">
        <c:forEach var="f" items="${files}">
          <li class="file-item" data-name="${fn:escapeXml(f.label)}"><a href="${ctx}${f.url}"><c:out value="${f.label}"/></a></li>
        </c:forEach>
      </ul>
    </details>
  </c:otherwise>
</c:choose>
</t:layout>
