<t:layout title="${pageTitle}" active="modules">
<div class="crumbs"><a href="${ctx}/">Home</a><span class="sep">/</span>Modules</div>
<h1>Modules</h1>
<p class="lede">System modules (compiler, runtime, HTTP servers, native subsystems) and the standard library packages. Each card says what the module is, in its own words, and how many files, SLeeLa units and documents it holds. Open a module to see its classes, documents and folders.</p>
<input class="filter" type="search" placeholder="Filter modules by name or description..." data-filter-list=".mod-card" aria-label="Filter modules">
<h2>System modules <span class="pill">${fn:length(sysModules)}</span></h2>
<div class="grid wide">
  <c:forEach var="c" items="${sysModules}">
    <a class="card mod-card" data-name="${fn:escapeXml(c.searchText)}" href="${ctx}${c.module.url}">
      <h3><c:out value="${c.module.name}"/></h3>
      <c:if test="${not empty c.summary.description}"><p><c:out value="${c.summary.description}"/></p></c:if>
      <p class="counts"><c:if test="${c.module.readme}"><span class="pill">README</span></c:if><span class="pill"><c:out value="${c.summary.files}"/> files</span><c:if test="${c.summary.units gt 0}"><span class="pill"><c:out value="${c.summary.units}"/> units</span></c:if><c:if test="${c.summary.docs gt 0}"><span class="pill"><c:out value="${c.summary.docs}"/> docs</span></c:if></p>
    </a>
  </c:forEach>
</div>
<h2>Library modules <span class="pill">${fn:length(libModules)}</span></h2>
<div class="grid wide">
  <c:forEach var="c" items="${libModules}">
    <a class="card mod-card" data-name="${fn:escapeXml(c.searchText)}" href="${ctx}${c.module.url}">
      <h3><c:out value="${c.module.name}"/></h3>
      <c:if test="${not empty c.summary.description}"><p><c:out value="${c.summary.description}"/></p></c:if>
      <p class="counts"><c:if test="${c.module.readme}"><span class="pill">README</span></c:if><c:if test="${c.module.api}"><span class="pill green">API</span></c:if><span class="pill"><c:out value="${c.summary.files}"/> files</span><c:if test="${c.summary.units gt 0}"><span class="pill"><c:out value="${c.summary.units}"/> units</span></c:if><c:if test="${c.summary.docs gt 0}"><span class="pill"><c:out value="${c.summary.docs}"/> docs</span></c:if></p>
    </a>
  </c:forEach>
</div>
</t:layout>
