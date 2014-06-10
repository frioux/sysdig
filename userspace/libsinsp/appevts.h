/*
Copyright (C) 2013-2014 Draios inc.

This file is part of sysdig.

sysdig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

sysdig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with sysdig.  If not, see <http://www.gnu.org/licenses/>.
*/

#define UESTORAGE_INITIAL_BUFSIZE 256

///////////////////////////////////////////////////////////////////////////////
// app table entry
///////////////////////////////////////////////////////////////////////////////
class sinsp_partial_appevt
{
public:
	sinsp_partial_appevt()
	{
		m_tags_storage = (char*)malloc(UESTORAGE_INITIAL_BUFSIZE);
		m_argnames_storage = (char*)malloc(UESTORAGE_INITIAL_BUFSIZE);
		m_argvals_storage = (char*)malloc(UESTORAGE_INITIAL_BUFSIZE);
		m_tags_storage_size = UESTORAGE_INITIAL_BUFSIZE;
		m_argnames_storage_size = UESTORAGE_INITIAL_BUFSIZE;
		m_argvals_storage_size = UESTORAGE_INITIAL_BUFSIZE;
	}

	~sinsp_partial_appevt()
	{
		if(m_tags_storage)
		{
			free(m_tags_storage);
		}

		if(m_argnames_storage)
		{
			free(m_argnames_storage); 
		}

		if(m_argvals_storage)
		{
			free(m_argvals_storage); 
		}
	}

	inline bool compare(sinsp_partial_appevt* other)
	{
		if(m_id != other->m_id)
		{
			return false;
		}

		if(memcmp(m_tags_storage, 
			other->m_tags_storage,
			MIN(m_tags_len, other->m_tags_len)) == 0)
		{
			return true;
		}

		return false;
	}

	char* m_tags_storage;
	char* m_argnames_storage;
	char* m_argvals_storage;
	uint32_t m_tags_len;
	uint32_t m_argnames_len;
	uint32_t m_argvals_len;
	uint32_t m_tags_storage_size;
	uint32_t m_argnames_storage_size;
	uint32_t m_argvals_storage_size;
	uint64_t m_id;
	vector<char*> m_tags;
	vector<char*> m_argnames;
	vector<char*> m_argvals;
	vector<uint32_t> m_taglens;
	vector<uint32_t> m_argnamelens;
	vector<uint32_t> m_argvallens;
	uint32_t m_ntags;
	uint32_t m_nargs;

	uint64_t m_time;
};

///////////////////////////////////////////////////////////////////////////////
// app event parser
///////////////////////////////////////////////////////////////////////////////
class sinsp_appevtparser
{
public:
	enum parse_result
	{
		RES_OK = 0,
		RES_COMMA = 1,
		RES_FAILED = 2,
		RES_TRUNCATED = 3,
	};

	sinsp_appevtparser(sinsp *inspector)
	{
		m_inspector = inspector;
		m_storage_size = 0;
		m_storage = NULL;
		m_res = sinsp_appevtparser::RES_OK;
		m_fragment_size = 0;
		m_enter_pae = NULL;
	}

	~sinsp_appevtparser()
	{
		if(m_storage)
		{
			free(m_storage);
		}
	}

	uint32_t get_storage_size()
	{
		return m_storage_size;
	}

	void set_storage_size(uint32_t newsize)
	{
		m_storage = (char*)realloc(m_storage, newsize);
		if(m_storage == NULL)
		{
			throw sinsp_exception("memory allocation error in sinsp_appevtparser::process_event_data.");
		}

		m_storage_size = newsize;
	}

	inline sinsp_appevtparser::parse_result process_event_data(char *data, uint32_t datalen, uint64_t ts)
	{
		ASSERT(data != NULL);

		//
		// Make sure we have enough space in the buffer and copy the data into it
		//
		if(m_storage_size < m_fragment_size + datalen + 1)
		{
			set_storage_size(m_fragment_size + datalen + 1);
		}

		memcpy(m_storage + m_fragment_size, data, datalen);
		m_storage[m_fragment_size + datalen] = 0;

		if(m_fragment_size != 0)
		{
			m_fullfragment_storage_str = m_storage;
		}

		//
		// Do the parsing
		//
		parse(m_storage, m_fragment_size + datalen);

		if(m_res == sinsp_appevtparser::RES_FAILED)
		{
			//
			// Invalid syntax
			//
			m_fragment_size = 0;
			m_fullfragment_storage_str.clear();
			return m_res;
		}
		else if(m_res == sinsp_appevtparser::RES_TRUNCATED)
		{
			//
			// Valid syntax, but the message is incomplete. Buffer it and wait for
			// more fragments.
			//
			if(m_fragment_size > MAX_USER_EVT_BUFFER)
			{
				//
				// Maximum buffering size reached, drop the event
				//
				m_fragment_size = 0;
				return m_res;
			}

			if(m_fullfragment_storage_str.length() == 0)
			{
				memcpy(m_storage, 
					data, 
					datalen);

				m_storage[datalen] = 0;
				m_fragment_size += datalen;
			}
			else
			{
				uint32_t tlen = (uint32_t)m_fullfragment_storage_str.length();

				memcpy(m_storage, 
					m_fullfragment_storage_str.c_str(), 
					tlen);

				m_fragment_size = tlen;
			}
		
			return m_res;
		}

		m_fragment_size = 0;
		m_fullfragment_storage_str.clear();

		//
		// Parser tests stop here
		//
#ifdef DR_TEST_APPEVT_PARSER
		return sinsp_appevtparser::RES_OK;
#endif

		//
		// Event decoding done. We do state tracking only if explicitly requested
		// by one or more filters.
		//
		if(m_inspector->m_track_appevts_state == false)
		{
			return sinsp_appevtparser::RES_OK;
		}

		//
		// If this is an enter event, allocate a sinsp_partial_appevt object and
		// push it to the list
		//
		if(m_type_str[0] == '>')
		{
			sinsp_partial_appevt* pae = m_inspector->m_partial_appevts_pool->pop();
			if(pae == NULL)
			{
				return sinsp_appevtparser::RES_OK;
			}

			init_partial_appevt(pae);
			pae->m_time = ts;
			m_inspector->m_partial_appevts_list.push_front(pae);
			m_enter_pae = pae;
		}
		else
		{
			list<sinsp_partial_appevt*>* partial_appevts_list = &m_inspector->m_partial_appevts_list;
			list<sinsp_partial_appevt*>::iterator it;

			init_partial_appevt(&m_exit_pae);

			for(it = partial_appevts_list->begin(); it != partial_appevts_list->end(); ++it)
			{
				if(m_exit_pae.compare(*it) == true)
				{
					m_exit_pae.m_time = ts;

					//
					// This is a bit tricky and deserves some explanation:
					// despite removing the pae and retunring it to the available pool,
					// we link to it so that the filters will use it. We do that as an
					// optimization (it avoids making a copy or implementing logic for 
					// delayed list removal), and we base it on the assumption that,
					// since the processing is strictly sequential and single thread,
					// nobody will modify the event until the event is fully processed.
					//
					m_enter_pae = *it;

					m_inspector->m_partial_appevts_pool->push(*it);
					partial_appevts_list->erase(it);
					return sinsp_appevtparser::RES_OK;
				}
			}

			m_enter_pae = NULL;
			ASSERT(false);
		}

		return sinsp_appevtparser::RES_OK;
	}

	inline void parse(char* evtstr, uint32_t evtstrlen)
	{
		char* p;
		uint32_t delta;
		char* tstr;

		//
		// Reset the content
		//
		p = m_storage;
		m_tags.clear();
		m_argnames.clear();
		m_argvals.clear();
		m_taglens.clear();
		m_argnamelens.clear();
		m_argvallens.clear();
		m_tot_taglens = 0;
		m_tot_argnamelens = 0;
		m_tot_argvallens = 0;

		//
		// Skip the initial braket
		//
		m_res = skip_spaces(p, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		if(*(p++) != '[')
		{
			m_res = sinsp_appevtparser::RES_FAILED;
			return;
		}

		//
		// type
		//
		m_res = parsestr(p, &m_type_str, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		//
		// ID
		//
		m_res = skip_spaces_and_commas(p, &delta, 1);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		m_res = parsenumber(p, &m_id, &delta);
		if(m_res > sinsp_appevtparser::RES_COMMA)
		{
			return;
		}
		p += delta;

		if(m_res == sinsp_appevtparser::RES_COMMA)
		{
			m_res = skip_spaces(p, &delta);
		}
		else
		{
			m_res = skip_spaces_and_commas(p, &delta, 1);
		}

		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		//
		// First tag
		//
		m_res = skip_spaces_and_char(p, &delta, '[');
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		m_res = parsestr_not_enforce(p, &tstr, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		if(tstr != NULL)
		{
			m_tags.push_back(tstr);
			m_taglens.push_back(delta - 2);
			m_tot_taglens += delta - 2;

			//
			// Remaining tags
			//
			while(true)
			{
				m_res = skip_spaces_and_commas(p, &delta, 0);
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;

				if(*p == ']')
				{
					break;
				}

				m_res = parsestr(p, &tstr, &delta);
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;
				m_tags.push_back(tstr);
				m_taglens.push_back(delta - 2);
				m_tot_taglens += delta - 2;
			}
		}

		//
		// First argument
		//
		m_res = skip_spaces_and_commas_and_all_brakets(p, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		m_res = parsestr_not_enforce(p, &tstr, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;
	
		if(tstr != NULL)
		{
			m_argnames.push_back(tstr);
			m_argnamelens.push_back(delta - 2);
			m_tot_argnamelens += delta - 2;

			m_res = skip_spaces_and_char(p, &delta, ':');
			if(m_res != sinsp_appevtparser::RES_OK)
			{
				return;
			}
			p += delta;

			m_res = parsestr(p, &tstr, &delta);
			if(m_res != sinsp_appevtparser::RES_OK)
			{
				return;
			}
			p += delta;
			m_argvals.push_back(tstr);
			m_argvallens.push_back(delta - 2);
			m_tot_argvallens += delta - 2;

			//
			// Remaining arguments
			//
			while(true)
			{
				m_res = skip_spaces_and_commas_and_cr_brakets(p, &delta);
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;

				if(*p == ']')
				{
					p++;
					break;
				}

				m_res = parsestr(p, &tstr, &delta);
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;
				m_argnames.push_back(tstr);
				m_argnamelens.push_back(delta - 2);
				m_tot_argnamelens += delta - 2;

				m_res = skip_spaces_and_char(p, &delta, ':');
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;

				m_res = parsestr(p, &tstr, &delta);
				if(m_res != sinsp_appevtparser::RES_OK)
				{
					return;
				}
				p += delta;
				m_argvals.push_back(tstr);
				m_argvallens.push_back(delta - 2);
				m_tot_argvallens += delta - 2;
			}
		}

		//
		// Terminating ]
		//
		m_res = skip_spaces(p, &delta);
		if(m_res != sinsp_appevtparser::RES_OK)
		{
			return;
		}
		p += delta;

		if(*p != ']')
		{
			if(*p == 0)
			{
				m_res = sinsp_appevtparser::RES_TRUNCATED;
			}
			else
			{
				m_res = sinsp_appevtparser::RES_FAILED;
			}
			return;
		}

		m_res = sinsp_appevtparser::RES_OK;
		return;
	}

	char* m_type_str;
	uint64_t m_id;
	vector<char*> m_tags;
	vector<char*> m_argnames;
	vector<char*> m_argvals;
	vector<uint32_t> m_taglens;
	vector<uint32_t> m_argnamelens;
	vector<uint32_t> m_argvallens;
	pair<vector<char*>*, vector<char*>*> m_args;
	uint32_t m_tot_taglens;
	uint32_t m_tot_argnamelens;
	uint32_t m_tot_argvallens;
	sinsp_partial_appevt* m_enter_pae;
	sinsp_partial_appevt m_exit_pae;

VISIBILITY_PRIVATE
	inline parse_result skip_spaces(char* p, uint32_t* delta)
	{
		char* start = p;

		while(*p == ' ')
		{
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}

			p++;
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result skip_spaces_and_commas(char* p, uint32_t* delta, uint32_t n_expected_commas)
	{
		char* start = p;
		uint32_t nc = 0;

		while(true)
		{
			if(*p == ' ')
			{
				p++;
				continue;
			}
			else if(*p == ',')
			{
				nc++;
			}
			else if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else
			{
				break;
			}

			p++;
		}

		if(nc < n_expected_commas)
		{
			return sinsp_appevtparser::RES_FAILED;
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result skip_spaces_and_char(char* p, uint32_t* delta, char char_to_skip)
	{
		char* start = p;
		uint32_t nc = 0;

		while(*p == ' ' || *p == char_to_skip || *p == 0)
		{
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else if(*p == char_to_skip)
			{
				nc++;
			}

			p++;
		}

		if(nc != 1)
		{
			return sinsp_appevtparser::RES_FAILED;
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result skip_spaces_and_commas_and_sq_brakets(char* p, uint32_t* delta)
	{
		char* start = p;
		uint32_t nc = 0;
		uint32_t nosb = 0;

		while(*p == ' ' || *p == ',' || *p == '[' || *p == ']' || *p == 0)
		{
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else if(*p == ',')
			{
				nc++;
			}
			else if(*p == '[')
			{
				nosb++;
			}
			else if(*p == ']')
			{
				if(nosb != 0)
				{
					break;
				}
			}

			p++;
		}

		if(nc != 1 || nosb != 1)
		{
			return sinsp_appevtparser::RES_FAILED;
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result skip_spaces_and_commas_and_cr_brakets(char* p, uint32_t* delta)
	{
		char* start = p;
		uint32_t nc = 0;
		uint32_t nocb = 0;
		uint32_t nccb = 0;

		while(*p == ' ' || *p == ',' || *p == '{' || *p == '}' || *p == 0)
		{
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else if(*p == ',')
			{
				nc++;
			}
			else if(*p == '{')
			{
				nocb++;
			}
			else if(*p == '}')
			{
				nccb++;
			}

			p++;
		}

		if(!((nc == 1 && nocb == 1) || (nc == 1 && nccb == 1) || (nccb == 1 && *p == ']')))
		{
			return sinsp_appevtparser::RES_FAILED;
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result skip_spaces_and_commas_and_all_brakets(char* p, uint32_t* delta)
	{
		char* start = p;
		uint32_t nc = 0;
		uint32_t nosb = 0;
		uint32_t nocb = 0;

		while(*p == ' ' || *p == ',' || *p == '[' || *p == ']' || *p == '{' || *p == '}' || (*p == 0))
		{
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else if(*p == ',')
			{
				nc++;
			}
			else if(*p == '[')
			{
				nosb++;
			}
			else if(*p == ']')
			{
				if(nosb != 0)
				{
					break;
				}
			}
			else if(*p == '{')
			{
				nocb++;
			}

			p++;
		}

		if(nc != 1 || nosb != 1)
		{
			return sinsp_appevtparser::RES_FAILED;
		}
		else if(nocb != 1)
		{
			if(*p != ']')
			{
				return sinsp_appevtparser::RES_FAILED;
			}
		}

		*delta = (uint32_t)(p - start);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result parsestr(char* p, char** res, uint32_t* delta)
	{
		char* initial = p;
		*res = NULL;

		//
		// Make sure that we start with a \"
		//
		if(*p != '"')
		{
			*delta = (uint32_t)(p - initial + 1);
			if(*p == 0)
			{
				return sinsp_appevtparser::RES_TRUNCATED;
			}
			else
			{
				return sinsp_appevtparser::RES_FAILED;
			}
		}

		*res = p + 1;
		p++;

		//
		// Navigate to the end of the string
		//
		while(!(*p == '\"' && *(p - 1) != '\\'))
		{
			if(*p == 0)
			{
				*delta = (uint32_t)(p - initial + 1);
				return sinsp_appevtparser::RES_TRUNCATED;
			}

			p++;
		}

		*p = 0;

		*delta = (uint32_t)(p - initial + 1);
		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result parsestr_not_enforce(char* p, char** res, uint32_t* delta)
	{
		sinsp_appevtparser::parse_result psres = parsestr(p, res, delta);

		if(psres == sinsp_appevtparser::RES_FAILED)
		{
			if(*(p + *delta) == ']')
			{
				*res = NULL;
				return sinsp_appevtparser::RES_OK;
			}
		}
		else if(psres == sinsp_appevtparser::RES_TRUNCATED)
		{
			return psres;
		}

		return sinsp_appevtparser::RES_OK;
	}

	inline parse_result parsenumber(char* p, uint64_t* res, uint32_t* delta)
	{
		char* start = p;
		sinsp_appevtparser::parse_result retval = sinsp_appevtparser::RES_OK;
		uint64_t val = 0;

		while(*p >= '0' && *p <= '9')
		{
			val = val * 10 + (*p - '0');
			p++;
		}

		if(*p == ',')
		{
			retval = sinsp_appevtparser::RES_COMMA;
		}
		else if(*p != 0 && *p != ' ')
		{
			return sinsp_appevtparser::RES_FAILED;
		}
		else if(*p == 0)
		{
			return sinsp_appevtparser::RES_TRUNCATED;
		}


		*p = 0;

		*res = val;
		*delta = (uint32_t)(p - start + 1);
		return retval;
	}

/*
	inline parse_result parsenumber(char* p, char** res, uint32_t* delta)
	{
		char* start = p;
		sinsp_appevtparser::parse_result retval = sinsp_appevtparser::RES_OK;

		*res = p;

		while(*p >= '0' && *p <= '9')
		{
			p++;
		}

		if(*p == ',')
		{
			retval = sinsp_appevtparser::RES_COMMA;
		}
		else if(*p != 0 && *p != ' ')
		{
			return sinsp_appevtparser::RES_FAILED;
		}
		else if(*p == 0)
		{
			return sinsp_appevtparser::RES_TRUNCATED;
		}


		*p = 0;

		*delta = (p - start + 1);
		return retval;
	}
*/
	inline void init_partial_appevt(sinsp_partial_appevt* pae)
	{
		vector<char*>::iterator it;
		vector<uint32_t>::iterator sit;

		//
		// Store the ID
		//
		pae->m_id = m_id;

		ASSERT(m_tags.size() == m_taglens.size());
		ASSERT(m_argnames.size() == m_argnamelens.size());
		ASSERT(m_argvals.size() == m_argvallens.size());

		//
		// Pack the tags
		//
		pae->m_tags.clear();
		pae->m_taglens.clear();
		pae->m_ntags = (uint32_t)m_tags.size();
		uint32_t encoded_tags_len = m_tot_taglens + pae->m_ntags + 1;

		if(pae->m_tags_storage_size < encoded_tags_len)
		{
			pae->m_tags_storage = (char*)realloc(pae->m_tags_storage, encoded_tags_len);
			pae->m_tags_storage_size = encoded_tags_len;
		}
		
		char* p = pae->m_tags_storage;
		for(it = m_tags.begin(), sit = m_taglens.begin(); 
			it != m_tags.end(); ++it, ++sit)
		{
			memcpy(p, *it, (*sit) + 1);
			pae->m_tags.push_back(p);
			pae->m_taglens.push_back(*sit);
			p += (*sit) + 1;
		}

		*p++ = 0;
		pae->m_tags_len = (uint32_t)(p - pae->m_tags_storage);

		//
		// Pack the argnames
		//
		pae->m_argnames.clear();
		pae->m_argnamelens.clear();
		pae->m_nargs = (uint32_t)m_argnames.size();
		uint32_t encoded_argnames_len = m_tot_argnamelens + pae->m_nargs + 1;

		if(pae->m_argnames_storage_size < encoded_argnames_len)
		{
			pae->m_argnames_storage = (char*)realloc(pae->m_argnames_storage, encoded_argnames_len);
			pae->m_argnames_storage_size = encoded_argnames_len;
		}
		
		p = pae->m_argnames_storage;
		for(it = m_argnames.begin(), sit = m_argnamelens.begin(); 
			it != m_argnames.end(); ++it, ++sit)
		{
			memcpy(p, *it, (*sit) + 1);
			pae->m_argnames.push_back(p);
			pae->m_argnamelens.push_back(*sit);
			p += (*sit) + 1;
		}

		*p++ = 0;
		pae->m_argnames_len = (uint32_t)(p - pae->m_argnames_storage);

		//
		// Pack the argvals
		//
		pae->m_argvals.clear();
		pae->m_argvallens.clear();
		uint32_t encoded_argvals_len = m_tot_argvallens + pae->m_nargs + 1;

		if(pae->m_argvals_storage_size < encoded_argvals_len)
		{
			pae->m_argvals_storage = (char*)realloc(pae->m_argvals_storage, encoded_argvals_len);
			pae->m_argvals_storage_size = encoded_argvals_len;
		}
		
		p = pae->m_argvals_storage;
		for(it = m_argvals.begin(), sit = m_argvallens.begin(); 
			it != m_argvals.end(); ++it, ++sit)
		{
			memcpy(p, *it, (*sit) + 1);
			pae->m_argvals.push_back(p);
			pae->m_argvallens.push_back(*sit);
			p += (*sit) + 1;
		}

		*p++ = 0;
		pae->m_argvals_len = (uint32_t)(p - pae->m_argvals_storage);
	}

	sinsp *m_inspector;
	char* m_storage;
	uint32_t m_storage_size;
	uint32_t m_fragment_size;
	sinsp_appevtparser::parse_result m_res;
	string m_fullfragment_storage_str;

	friend class sinsp_parser;
};