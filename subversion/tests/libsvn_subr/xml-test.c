/* xml-test.c --- tests for the XML parser
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_xml.h"

#include "../svn_test.h"

/* Implements svn_xml_start_elem. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_start_elem(void *baton, const char *name, const char **atts)
{
  svn_stringbuf_t *buf = baton;
  svn_stringbuf_appendcstr(buf, "<");
  svn_stringbuf_appendcstr(buf, name);
  while (*atts)
  {
    svn_stringbuf_appendcstr(buf, " ");
    svn_stringbuf_appendcstr(buf, atts[0]);
    svn_stringbuf_appendcstr(buf, "=");
    svn_stringbuf_appendcstr(buf, atts[1]);
    atts += 2;
  }
  svn_stringbuf_appendcstr(buf, ">");
}

/* Implements svn_xml_end_elem. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_end_elem(void *baton, const char *name)
{
  svn_stringbuf_t *buf = baton;
  svn_stringbuf_appendcstr(buf, "</");
  svn_stringbuf_appendcstr(buf, name);
  svn_stringbuf_appendcstr(buf, ">");
}

/* Implements svn_xml_char_data. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_cdata(void *baton, const char *data, apr_size_t len)
{
  svn_stringbuf_t *buf = baton;
  svn_stringbuf_appendbytes(buf, data, len);
}

/* Implements svn_xml_end_elem. Callback used in test_signal_bailout and
 * test_invalid_xml_signal_bailout tests.  BATON is 'svn_xml_parser_t **'. */
static void
err_end_elem(void *baton, const char *name)
{
  svn_xml_parser_t **parser = baton;

  svn_xml_signal_bailout(svn_error_create(APR_EGENERAL, NULL, NULL),
                         *parser);
}

static svn_error_t *
test_simple(apr_pool_t *pool)
{
  const char *xml = "<root><tag1>value</tag1><tag2 a='v' /></root>";
  const char *p;
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);
  svn_xml_parser_t *parser;

  /* Test parsing XML in one chunk.*/
  parser = svn_xml_make_parser(buf, strbuf_start_elem, strbuf_end_elem,
                               strbuf_cdata, pool);

  SVN_ERR(svn_xml_parse(parser, xml, strlen(xml), TRUE));

  SVN_TEST_STRING_ASSERT(buf->data,
                         "<root><tag1>value</tag1><tag2 a=v></tag2></root>");
  svn_xml_free_parser(parser);

  /* Test parsing XML byte by byte.*/
  buf = svn_stringbuf_create_empty(pool);

  parser = svn_xml_make_parser(buf, strbuf_start_elem, strbuf_end_elem,
                               strbuf_cdata, pool);

  for (p = xml; *p; p++)
    {
      SVN_ERR(svn_xml_parse(parser, p, 1, FALSE));
    }
  SVN_ERR(svn_xml_parse(parser, NULL, 0, TRUE));
  svn_xml_free_parser(parser);

  SVN_TEST_STRING_ASSERT(buf->data,
                         "<root><tag1>value</tag1><tag2 a=v></tag2></root>");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_invalid_xml(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1>value</tag1>";
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);
  svn_xml_parser_t *parser;
  svn_error_t *err;

  parser = svn_xml_make_parser(buf, strbuf_start_elem, strbuf_end_elem,
                               strbuf_cdata, pool);

  err = svn_xml_parse(parser, xml, strlen(xml), TRUE);

  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_XML_MALFORMED);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_signal_bailout(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1></tag1></root>";
  svn_xml_parser_t *parser;
  svn_error_t *err;

  parser = svn_xml_make_parser(&parser, NULL, err_end_elem, NULL, pool);
  err = svn_xml_parse(parser, xml, strlen(xml), TRUE);
  SVN_TEST_ASSERT_ERROR(err, APR_EGENERAL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_invalid_xml_signal_bailout(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1></tag1>";
  svn_xml_parser_t *parser;
  svn_error_t *err;

  parser = svn_xml_make_parser(&parser, NULL, err_end_elem, NULL, pool);
  err = svn_xml_parse(parser, xml, strlen(xml), TRUE);

  /* We may get SVN_ERR_XML_MALFORMED or error from err_end_elem() callback.
   * This behavior depends how XML parser works: it may pre-parse data before
   * callback invocation. */
  SVN_TEST_ASSERT_ANY_ERROR(err);

  if (err->apr_err != SVN_ERR_XML_MALFORMED &&
      err->apr_err != APR_EGENERAL)
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "Got unxpected error '%s'",
                               svn_error_symbolic_name(err->apr_err));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_parser_free(apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  /* Test explicit svn_xml_free_parser() calls. */
  iterpool = svn_pool_create(pool);
  for (i = 0; i < 100; i++)
  {
      svn_xml_parser_t *parser;

      svn_pool_clear(iterpool);

      parser = svn_xml_make_parser(&parser, NULL, NULL, NULL, iterpool);
      svn_xml_free_parser(parser);
  }
  svn_pool_destroy(iterpool);

  /* Test parser free using pool cleanup. */
  iterpool = svn_pool_create(pool);
  for (i = 0; i < 100; i++)
  {
      svn_xml_parser_t *parser;

      svn_pool_clear(iterpool);

      parser = svn_xml_make_parser(&parser, NULL, NULL, NULL, iterpool);
      /* We didn't call svn_xml_free_parser(): the parser will be freed on
         pool cleanup. */
  }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* The test table.  */
static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_simple,
                   "simple XML parser test"),
    SVN_TEST_PASS2(test_invalid_xml,
                   "invalid XML test"),
    SVN_TEST_PASS2(test_signal_bailout,
                   "test svn_xml_signal_bailout()"),
    SVN_TEST_PASS2(test_invalid_xml_signal_bailout,
                   "test svn_xml_signal_bailout() for invalid XML"),
    SVN_TEST_PASS2(test_parser_free,
                   "test svn_xml_parser_free()"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN