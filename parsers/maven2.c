/*
 *
 *   Copyright (c) 2016, Masatake YAMATO
 *   Copyright (c) 2016, Red Hat, K.K.
 *
 *   This source code is released for free distribution under the terms of the
 *   GNU General Public License version 2 or (at your option) any later version.
 *
 *   This module contains functions for generating tags for maven2 project model
 *   defined in http://maven.apache.org/POM/4.0.0,
 *              http://maven.apache.org/maven-v4_0_0.xsd.
 */

#include "general.h"	/* must always come first */

#include "debug.h"
#include "options.h"
#include "parse.h"
#include "read.h"
#include "routines.h"
#include "selectors.h"

#ifdef HAVE_LIBXML
#include <libxml/xpath.h>
#include <libxml/tree.h>
#endif

#include <string.h>


typedef enum {
	K_GROUP_ID, K_ARTIFACT_ID, K_PROPERTY,
} maven2Kind;

typedef enum {
	R_GROUP_ID_PARENT,
} maven2GroupIdRole;

typedef enum {
	R_ARTIFACT_ID_PARENT,
} maven2ArtifactIdRole;

static roleDesc Maven2GroupIdRoles [] = {
	{ TRUE, "parent", "parent" },
};

static roleDesc Maven2ArtifactIdRoles [] = {
	{ TRUE, "parent", "parent" },
};

static kindOption Maven2Kinds [] = {
	{ TRUE,  'g', "groupId",    "group identifiers",
	  .referenceOnly = FALSE, ATTACH_ROLES (Maven2GroupIdRoles) },
	{ TRUE,  'a', "artifactId", "artifact identifiers",
	  .referenceOnly = FALSE, ATTACH_ROLES (Maven2ArtifactIdRoles) },
	{ TRUE,  'p', "property",   "properties" },
};

static void makeTagWithScope (xmlNode *node,
			      const struct sTagXpathMakeTagSpec *spec,
			      struct sTagEntryInfo *tag,
			      void *userData);


static void makeTagForProperties (xmlNode *node,
				  const struct sTagXpathRecurSpec *spec __unused__,
				  xmlXPathContext *ctx __unused__,
				  void *userData)
{
	const xmlChar* str;
	tagEntryInfo tag;
	int *corkIndexes = userData;

	str = node->name;
	initTagEntry (&tag, (char *)str, Maven2Kinds + K_PROPERTY);
	tag.lineNumber = xmlGetLineNo (node);
	tag.filePosition = getInputFilePositionForLine (tag.lineNumber);

	if ( corkIndexes [K_ARTIFACT_ID] != SCOPE_NIL )
		tag.extensionFields.scopeIndex = corkIndexes [K_ARTIFACT_ID];

	makeTagEntry (&tag);
}

static tagXpathTable maven2XpathMainTable[] = {
	{ "/*[local-name()='project']/*[local-name()='groupId']",
	  LXPATH_TABLE_DO_MAKE,
	  { .makeTagSpec = { K_GROUP_ID, ROLE_INDEX_DEFINITION,
			     makeTagWithScope } }
	},
	{ "/*[local-name()='project']/*[local-name()='parent']/*[local-name()='groupId']",
	  LXPATH_TABLE_DO_MAKE,
	  { .makeTagSpec = { K_GROUP_ID,  R_GROUP_ID_PARENT,
			     makeTagWithScope } }
	},
	{ "/*[local-name()='project']/*[local-name()='artifactId']",
	  LXPATH_TABLE_DO_MAKE,
	  { .makeTagSpec = { K_ARTIFACT_ID, ROLE_INDEX_DEFINITION,
			     makeTagWithScope } }
	},
	{ "/*[local-name()='project']/*[local-name()='parent']/*[local-name()='artifactId']",
	  LXPATH_TABLE_DO_MAKE,
	  { .makeTagSpec = { K_ARTIFACT_ID,  R_ARTIFACT_ID_PARENT,
			     makeTagWithScope } }
	},
	{ "/*[local-name()='project']/*[local-name()='properties']/*",
	  LXPATH_TABLE_DO_RECUR,
	  { .recurSpec = { makeTagForProperties } }
	},
};

enum maven2XpathTable {
	TABLE_MAIN,
};

static tagXpathTableTable maven2XpathTableTable[] = {
	[TABLE_MAIN] = { ARRAY_AND_SIZE(maven2XpathMainTable) },
};

typedef enum {
	F_VERSION,
} maven2Field;

static fieldSpec Maven2Fields [] = {
	{
		.name = "version",
		.description = "version of artifact",
		.enabled = FALSE,
	}
};

static void attachVersionIfExisting (struct sTagEntryInfo *tag, xmlNode *node)
{
	const char *version = NULL;

#ifdef HAVE_LIBXML
	for (node = node->next; node != NULL; node = node->next)
	{
		if (strcmp ((char *)node->name, "version") == 0)
		{
			version = (const char *)xmlNodeGetContent (node);
			break;
		}
	}
#endif
	if (version)
		attachField (Maven2Fields [F_VERSION].ftype, tag, version);
}

static void makeTagWithScope (xmlNode *node,
			      const struct sTagXpathMakeTagSpec *spec,
			      struct sTagEntryInfo *tag,
			      void *userData)
{
	int *corkIndexes = userData;
	int i;


	if (tag->kind == Maven2Kinds + K_ARTIFACT_ID)
		attachVersionIfExisting (tag, node);

	i = makeTagEntry (tag);

	if (((tag->kind == Maven2Kinds + K_GROUP_ID)
	     && (tag->extensionFields.roleIndex == ROLE_INDEX_DEFINITION))
	    || ((tag->kind == Maven2Kinds + K_ARTIFACT_ID)
		&& (tag->extensionFields.roleIndex == ROLE_INDEX_DEFINITION)))
	{
		corkIndexes [spec->kind] = i;
	}
}

static void
findMaven2Tags (void)
{
	int corkIndexes [] = {
		[K_GROUP_ID]    = SCOPE_NIL,
		[K_ARTIFACT_ID] = SCOPE_NIL,
	};


	findXMLTags (NULL, NULL, maven2XpathTableTable + TABLE_MAIN, Maven2Kinds, &corkIndexes);

	if ( corkIndexes [K_ARTIFACT_ID] != SCOPE_NIL
	     && corkIndexes [K_GROUP_ID] != SCOPE_NIL)
	{
		tagEntryInfo *tag = getEntryInCorkQueue (corkIndexes [K_ARTIFACT_ID]);
		tag->extensionFields.scopeIndex = corkIndexes [K_GROUP_ID];
	}
}

extern parserDefinition*
Maven2Parser (void)
{
	static const char *const extensions [] = { "pom", NULL };
	static const char *const patterns [] =   { "pom.xml", NULL };
	parserDefinition* const def = parserNew ("Maven2");
	static selectLanguage selectors[] = { selectByDTD, NULL };

	def->kinds         = Maven2Kinds;
	def->kindCount     = ARRAY_SIZE (Maven2Kinds);
	def->extensions    = extensions;
	def->patterns      = patterns;
	def->parser        = findMaven2Tags;
	def->tagXpathTableTable  = maven2XpathTableTable;
	def->tagXpathTableCount  = ARRAY_SIZE (maven2XpathTableTable);
	def->useCork = TRUE;
	def->selectLanguage = selectors;
	def->fieldSpecs = Maven2Fields;
	def->fieldSpecCount = ARRAY_SIZE (Maven2Fields);
	return def;
}

/* vi:set tabstop=4 shiftwidth=4: */
