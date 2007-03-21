<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:mc="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="mc">
  <!--Don't move the declaration of the HTML namespace up here - XMLNSs
  don't work ideally in the presence of two things that want to use the
  absence of a prefix, sadly. -->

  <xsl:template match="*" mode="identity">
    <xsl:copy>
      <xsl:apply-templates mode="identity"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="mc:docstring">
    <xsl:apply-templates select="node()" mode="identity"/>
  </xsl:template>

  <xsl:template match="mc:errors">
    <h1 xmlns="http://www.w3.org/1999/xhtml">Errors</h1>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="mc:error">
    <h2 xmlns="http://www.w3.org/1999/xhtml"><a name="{concat(../@namespace, '.', translate(@name, ' ', ''))}"></a><xsl:value-of select="concat(../@namespace, '.', translate(@name, ' ', ''))"/></h2>
    <xsl:apply-templates select="mc:docstring"/>
  </xsl:template>

  <xsl:template match="/mc:spec/mc:copyright">
    <div xmlns="http://www.w3.org/1999/xhtml">
      <xsl:apply-templates/>
    </div>
  </xsl:template>
  <xsl:template match="/mc:spec/mc:license">
    <div xmlns="http://www.w3.org/1999/xhtml" class="license">
      <xsl:apply-templates mode="identity"/>
    </div>
  </xsl:template>

  <xsl:template match="mc:copyright"/>
  <xsl:template match="mc:license"/>

  <xsl:template match="interface">
    <h1 xmlns="http://www.w3.org/1999/xhtml"><a name="{@name}"></a><xsl:value-of select="@name"/></h1>

    <xsl:if test="mc:requires">
      <p>Implementations of this interface must also implement:</p>
      <ul xmlns="http://www.w3.org/1999/xhtml">
        <xsl:for-each select="mc:requires">
          <li><code><xsl:value-of select="@interface"/></code></li>
        </xsl:for-each>
      </ul>
    </xsl:if>

    <xsl:apply-templates select="mc:docstring" />

    <xsl:choose>
      <xsl:when test="method">
        <h2 xmlns="http://www.w3.org/1999/xhtml">Methods:</h2>
        <xsl:apply-templates select="method"/>
      </xsl:when>
      <xsl:otherwise>
        <p xmlns="http://www.w3.org/1999/xhtml">Interface has no methods.</p>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:choose>
      <xsl:when test="signal">
        <h2 xmlns="http://www.w3.org/1999/xhtml">Signals:</h2>
        <xsl:apply-templates select="signal"/>
      </xsl:when>
      <xsl:otherwise>
        <p xmlns="http://www.w3.org/1999/xhtml">Interface has no signals.</p>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:choose>
      <xsl:when test="mc:property">
        <h2 xmlns="http://www.w3.org/1999/xhtml">Properties:</h2>
        <dl xmlns="http://www.w3.org/1999/xhtml">
          <xsl:apply-templates select="mc:property"/>
        </dl>
      </xsl:when>
      <xsl:otherwise>
        <p xmlns="http://www.w3.org/1999/xhtml">Interface has no properties.</p>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:if test="mc:enum">
      <h2 xmlns="http://www.w3.org/1999/xhtml">Enumerated types:</h2>
      <xsl:apply-templates select="mc:enum"/>
    </xsl:if>

    <xsl:if test="mc:flags">
      <h2 xmlns="http://www.w3.org/1999/xhtml">Sets of flags:</h2>
      <xsl:apply-templates select="mc:flags"/>
    </xsl:if>

  </xsl:template>

  <xsl:template match="mc:flags">
    <h3 xmlns="http://www.w3.org/1999/xhtml"><xsl:value-of select="@name"/></h3>
    <xsl:apply-templates select="mc:docstring" />
    <dl xmlns="http://www.w3.org/1999/xhtml">
        <xsl:variable name="value-prefix">
          <xsl:choose>
            <xsl:when test="@value-prefix">
              <xsl:value-of select="@value-prefix"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="@name"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
      <xsl:for-each select="mc:flag">
        <dt xmlns="http://www.w3.org/1999/xhtml"><code><xsl:value-of select="concat($value-prefix, '_', @suffix)"/> = <xsl:value-of select="@value"/></code></dt>
        <xsl:choose>
          <xsl:when test="mc:docstring">
            <dd xmlns="http://www.w3.org/1999/xhtml"><xsl:apply-templates select="mc:docstring" /></dd>
          </xsl:when>
          <xsl:otherwise>
            <dd xmlns="http://www.w3.org/1999/xhtml">(Undocumented)</dd>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:for-each>
    </dl>
  </xsl:template>

  <xsl:template match="mc:enum">
    <h3 xmlns="http://www.w3.org/1999/xhtml"><xsl:value-of select="@name"/></h3>
    <xsl:apply-templates select="mc:docstring" />
    <dl xmlns="http://www.w3.org/1999/xhtml">
        <xsl:variable name="value-prefix">
          <xsl:choose>
            <xsl:when test="@value-prefix">
              <xsl:value-of select="@value-prefix"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:value-of select="@name"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
      <xsl:for-each select="mc:enumvalue">
        <dt xmlns="http://www.w3.org/1999/xhtml"><code><xsl:value-of select="concat($value-prefix, '_', @suffix)"/> = <xsl:value-of select="@value"/></code></dt>
        <xsl:choose>
          <xsl:when test="mc:docstring">
            <dd xmlns="http://www.w3.org/1999/xhtml"><xsl:apply-templates select="mc:docstring" /></dd>
          </xsl:when>
          <xsl:otherwise>
            <dd xmlns="http://www.w3.org/1999/xhtml">(Undocumented)</dd>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:for-each>
    </dl>
  </xsl:template>

  <xsl:template match="mc:property">
    <dt xmlns="http://www.w3.org/1999/xhtml">
      <xsl:if test="@name">
        <code><xsl:value-of select="@name"/></code> -
      </xsl:if>
      <code><xsl:value-of select="@type"/></code>
    </dt>
    <dd xmlns="http://www.w3.org/1999/xhtml">
      <xsl:apply-templates select="mc:docstring"/>
    </dd>
  </xsl:template>

  <xsl:template match="method">
    <div xmlns="http://www.w3.org/1999/xhtml" class="method">
      <h3 xmlns="http://www.w3.org/1999/xhtml"><xsl:value-of select="@name"/> (
        <xsl:for-each xmlns="" select="arg[@direction='in']">
          <xsl:value-of select="@type"/>: <xsl:value-of select="@name"/>
          <xsl:if test="position() != last()">, </xsl:if>
        </xsl:for-each>
        ) &#x2192;
        <xsl:choose>
          <xsl:when test="arg[@direction='out']">
            <xsl:for-each xmlns="" select="arg[@direction='out']">
              <xsl:value-of select="@type"/>
              <xsl:if test="position() != last()">, </xsl:if>
            </xsl:for-each>
          </xsl:when>
          <xsl:otherwise>nothing</xsl:otherwise>
        </xsl:choose>
      </h3>
      <div xmlns="http://www.w3.org/1999/xhtml" class="docstring">
        <xsl:apply-templates select="mc:docstring" />
      </div>

      <xsl:if test="arg[@direction='in']">
        <div xmlns="http://www.w3.org/1999/xhtml">
          <h4>Parameters</h4>
          <dl xmlns="http://www.w3.org/1999/xhtml">
            <xsl:apply-templates select="arg[@direction='in']"
              mode="parameters-in-docstring"/>
          </dl>
        </div>
      </xsl:if>

      <xsl:if test="arg[@direction='out']">
        <div xmlns="http://www.w3.org/1999/xhtml">
          <h4>Returns</h4>
          <dl xmlns="http://www.w3.org/1999/xhtml">
            <xsl:apply-templates select="arg[@direction='out']"
              mode="returns-in-docstring"/>
          </dl>
        </div>
      </xsl:if>

      <xsl:if test="mc:possible-errors">
        <div xmlns="http://www.w3.org/1999/xhtml">
          <h4>Possible errors</h4>
          <dl xmlns="http://www.w3.org/1999/xhtml">
            <xsl:apply-templates select="mc:possible-errors/mc:error"/>
          </dl>
        </div>
      </xsl:if>

    </div>
  </xsl:template>

  <xsl:template match="arg" mode="parameters-in-docstring">
    <dt xmlns="http://www.w3.org/1999/xhtml">
      <code><xsl:value-of select="@name"/></code> -
      <code><xsl:value-of select="@type"/></code>
    </dt>
    <dd xmlns="http://www.w3.org/1999/xhtml">
      <xsl:apply-templates select="mc:docstring" />
    </dd>
  </xsl:template>

  <xsl:template match="arg" mode="returns-in-docstring">
    <dt xmlns="http://www.w3.org/1999/xhtml">
      <xsl:if test="@name">
        <code><xsl:value-of select="@name"/></code> -
      </xsl:if>
      <code><xsl:value-of select="@type"/></code>
    </dt>
    <dd xmlns="http://www.w3.org/1999/xhtml">
      <xsl:apply-templates select="mc:docstring"/>
    </dd>
  </xsl:template>

  <xsl:template match="mc:possible-errors/mc:error">
    <dt xmlns="http://www.w3.org/1999/xhtml">
      <code><xsl:value-of select="@name"/></code>
    </dt>
    <dd xmlns="http://www.w3.org/1999/xhtml">
        <xsl:variable name="name" select="@name"/>
        <xsl:choose>
          <xsl:when test="mc:docstring">
            <xsl:apply-templates select="mc:docstring"/>
          </xsl:when>
          <xsl:when test="//mc:errors/mc:error[concat(../@namespace, '.', translate(@name, ' ', ''))=$name]/mc:docstring">
            <xsl:apply-templates select="//mc:errors/mc:error[concat(../@namespace, '.', translate(@name, ' ', ''))=$name]/mc:docstring"/> <em xmlns="http://www.w3.org/1999/xhtml">(generic description)</em>
          </xsl:when>
          <xsl:otherwise>
            (Undocumented.)
          </xsl:otherwise>
        </xsl:choose>
    </dd>
  </xsl:template>

  <xsl:template match="signal">
    <div xmlns="http://www.w3.org/1999/xhtml" class="signal">
      <h3 xmlns="http://www.w3.org/1999/xhtml"><xsl:value-of select="@name"/> ( 
        <xsl:for-each xmlns="" select="arg">
          <xsl:value-of select="@type"/>: <xsl:value-of select="@name"/>
          <xsl:if test="position() != last()">, </xsl:if>
        </xsl:for-each>
        )</h3>
      <div xmlns="http://www.w3.org/1999/xhtml" class="docstring">
        <xsl:apply-templates select="mc:docstring"/>
      </div>

      <xsl:if test="arg">
        <div xmlns="http://www.w3.org/1999/xhtml">
          <h4>Parameters</h4>
          <dl xmlns="http://www.w3.org/1999/xhtml">
            <xsl:apply-templates select="arg" mode="parameters-in-docstring"/>
          </dl>
        </div>
      </xsl:if>
    </div>
  </xsl:template>

  <xsl:output method="xml" indent="no" encoding="ascii"
    omit-xml-declaration="yes"
    doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"
    doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN" />

  <xsl:template match="/mc:spec">
    <html xmlns="http://www.w3.org/1999/xhtml">
      <head>
        <title>MissionControl D-Bus Interface Specification</title>
        <style type="text/css">

          body {
            font-family: sans-serif;
            margin: 2em;
            height: 100%;
            font-size: 1.2em;
          }
          h1 {
            padding-top: 5px;
            padding-bottom: 5px;
            font-size: 1.6em;
            background: #dadae2;
          }
          h2 {
            font-size: 1.3em;
          }
          h3 {
            font-size: 1.2em;
          }
          a:link, a:visited, a:link:hover, a:visited:hover {
            font-weight: bold;
          }
          .topbox {
            padding-top: 10px;
            padding-left: 10px;
            border-bottom: black solid 1px;
            padding-bottom: 10px;
            background: #dadae2;
            font-size: 2em;
            font-weight: bold;
            color: #5c5c5c;
          }
          .topnavbox {
            padding-left: 10px;
            padding-top: 5px;
            padding-bottom: 5px;
            background: #abacba;
            border-bottom: black solid 1px;
            font-size: 1.2em;
          }
          .topnavbox a{
            color: black;
            font-weight: normal;
          }
          .sidebar {
            float: left;
            /* width:9em;
            border-right:#abacba solid 1px;
            border-left: #abacba solid 1px;
            height:100%; */
            border: #abacba solid 1px;
            padding-left: 10px;
            margin-left: 10px;
            padding-right: 10px;
            margin-right: 10px;
            color: #5d5d5d;
            background: #dadae2;
          }
          .sidebar a {
            text-decoration: none;
            border-bottom: #e29625 dotted 1px;
            color: #e29625;
            font-weight: normal;
          }
          .sidebar h1 {
            font-size: 1.2em;
            color: black;
          }
          .sidebar ul {
            padding-left: 25px;
            padding-bottom: 10px;
            border-bottom: #abacba solid 1px;
          }
          .sidebar li {
            padding-top: 2px;
            padding-bottom: 2px;
          }
          .sidebar h2 {
            font-style:italic;
            font-size: 0.81em;
            padding-left: 5px;
            padding-right: 5px;
            font-weight: normal;
          }
          .date {
            font-size: 0.6em;
            float: right;
            font-style: italic;
          }
          .method {
            margin-left: 1em;
            margin-right: 4em;
          }
          .signal {
            margin-left: 1em;
            margin-right: 4em;
          }

        </style>
      </head>
      <body>
        <h1 class="topbox">MissionControl D-Bus Interface Specification</h1>
        <xsl:apply-templates select="mc:copyright"/>
        <xsl:apply-templates select="mc:license"/>
        <xsl:apply-templates select="mc:docstring"/>
        <xsl:apply-templates select="node"/>
        <xsl:apply-templates select="mc:errors"/>
      </body>
    </html>
  </xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et: -->
