<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" 
  xmlns:yin="urn:ietf:params:xml:ns:yang:yin:1"
  xmlns:nacm="urn:ietf:params:xml:ns:yang:ietf-netconf-acm"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text"/>

<xsl:template match="/yin:module">
module <xsl:value-of select="@name" /> {
  <xsl:apply-templates select="*" />
}
</xsl:template>

<xsl:template match="yin:namespace">
  namespace "<xsl:value-of select="@uri"/>";
</xsl:template>

<xsl:template match="yin:import">
  import <xsl:value-of select="@module"/> {
    prefix "<xsl:value-of select="yin:prefix/@value" />";
  }
</xsl:template>

<xsl:template match="yin:*[@name]|yin:*[@value]">
  <xsl:choose>
    <xsl:when test="@name">
      <xsl:value-of select="local-name(.)" /> "<xsl:value-of select="@name" />"
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="local-name(.)" /> "<xsl:value-of select="@value" />"
    </xsl:otherwise>
  </xsl:choose>

  <xsl:choose>
    <xsl:when test="./node()" > {
      <xsl:apply-templates select="*" />
      }
    </xsl:when>
    <xsl:otherwise>;
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="yin:*[yin:text]" >
  <xsl:value-of select="local-name(.)" />
      "<xsl:value-of disable-output-escaping="yes" select="yin:text" />";
</xsl:template>

<xsl:template match="yin:revision|yin:input|yin:output">
  <xsl:value-of select="local-name(.)"/>
  <xsl:text> </xsl:text>
  <xsl:choose>
    <xsl:when test="local-name(.) = 'revision'">
      "<xsl:value-of select="@date"/>"
    </xsl:when>
    <xsl:otherwise/> <!-- input, output -->
  </xsl:choose>  
  <xsl:choose>
    <xsl:when test="./node()" > {
      <xsl:apply-templates select="*" />
      }
    </xsl:when>
    <xsl:otherwise>;
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="yin:*[@condition]">
  <xsl:value-of select="local-name(.)"/> "<xsl:value-of select="@condition"/>"
  <xsl:choose>
    <xsl:when test="./node()"> {
      <xsl:apply-templates select="*"/>
      }
    </xsl:when>
    <xsl:otherwise>;
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="nacm:*">
  <xsl:value-of select="name(.)"/>;
</xsl:template>

</xsl:stylesheet>
