#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <chrono>
#include <sstream>
#include <map>
#include <openssl/md5.h>
//value needed for the MD5 function
#define AES_BLOCK_SIZE 16
//access last n bits in k
#define LAST(k,n) ((k) & ((1<<(n))-1))
//access from m to n bytes in k
#define MID(k,m,n) LAST((k)>>(m),((n)-(m)))
#define HASH_EQUALS(hash1, hash2) hash1.l == hash2.l

//turn a RWord into an uint that can index an array 
//the first 8 characters are 1 bit
//the next 3 characters are 2 bits
//and 't' is 3 bits
#define SERIALIZE_RES(res)\
((unsigned int)res[0]\
| (res[1]<<1)\
| (res[2]<<2)\
| (res[3]<<3)\
| (res[4]<<4)\
| (res[5]<<5)\
| (res[6]<<6)\
| (res[7]<<7)\
| (res[8]<<8)\
| (res[9]<<10)\
| (res[10]<<12)\
| (res[11]<<14)\
)

typedef std::chrono::high_resolution_clock Clock;

//Character representation of a word
typedef struct {
	char chars[11];
	unsigned char size_bytes;
} CWord;

/**
*  Resources representation of a word
*	i:char:default
*	0:'n' :1
*	1:'a' :1
*	2:'i' :1
*	3:'w' :1
*	4:'y' :1
*	5:'r' :1
*	6:'l' :1
*	7:'p' :1
*	8:'s' :2
*	9:'u' :2
*  10:'o' :2
*  11:'t' :4
*/
typedef struct {
	unsigned char chars[12];
	CWord* cWord;
} RWord;

//Character representation of a word
typedef struct {
	char chars[36];
	unsigned char size_bytes;
}CPhrase;

//Union to hold hashes, it is used to convert the literals into the result of the MD5 function and to compare them
typedef union {
	unsigned long i[4];
	unsigned char c[16];
	unsigned long long l;
} PhraseHash;

//e4820b45d2277f3844eac66c903e84be
const PhraseHash correctHash1 = {
		{
			0x450b82e4,
			0x387f27d2,
			0x6cc6ea44,
			0xbe843e90
		}
};
//23170acc097c24edb98fc5488ab033fe
const PhraseHash correctHash2 = {
	{
		0xcc0a1723,
		0xed247c09,
		0x48c58fb9,
		0xfe33b08a
	}
};
//665e5bcb0c20062fe8abaaf4628bb154
const PhraseHash correctHash3 = {
	{
		0xcb5b5e66,
		0x2f06200c,
		0xf4aaabe8,
		0x54b18b62
	}
};
//maps numbers to characters
typedef struct {
	char chars[12];
}CharMap;

CharMap charMap = { {
	'n',
	'a',
	'i',
	'w',
	'y',
	'r',
	'l',
	'p',
	's',
	'u',
	'o',
	't'
} };

#define words_length  1659
#define res_index_size 131073
using namespace std;
//inverse of the charmap, maps characters to numbers
map<char, unsigned char> i_charMap;


//mutex for cout
mutex printMutex;
//mutex to acess the solution file
mutex solutionMutex;

//recursive function that checks each word
//the current bottleneck is the md5, could be solved using opencl, maybe I'll implement it in the future
//NOTE: you need to make the stack a bit bigger for it to work
void tpStep(ofstream* fout, CPhrase phrase, unsigned int currentRes, RWord currentResChars, RWord** gresArray, size_t* resArraySizes,
	size_t startIndex, string*statusFileName, int threads) {

	chrono::time_point<chrono::steady_clock> start;
	size_t j;
	RWord* resArray = gresArray[currentRes];
	size_t size = resArraySizes[currentRes];
	//Each compatible word
	for (size_t i = startIndex; i < size; i += threads) {
		// If it is at the bottom of the stack
		if (statusFileName != nullptr) {
			fout->open(*statusFileName);
			*fout << "current: " << i;
			printMutex.lock();
			cout << i << " t:" << chrono::duration_cast<chrono::seconds>(Clock::now() - start).count() << endl;
			fout->close();
			printMutex.unlock();
			start = Clock::now();
		}
		CPhrase lphrase = phrase;
		RWord word = resArray[i];
		RWord lResChars = currentResChars;
		//Substract the resources
		for (j = 0; j < 12; j++) {
			lResChars.chars[j] -= word.chars[j];
		}
		//Add the word to the phrase
		for (j = 0; j < word.cWord->size_bytes; j++) {
			lphrase.chars[lphrase.size_bytes++] = word.cWord->chars[j];
		}
		lphrase.chars[lphrase.size_bytes++] = ' ';
		currentRes = SERIALIZE_RES(lResChars.chars); //Turn the resources into an index
		if (currentRes == 0) {
			PhraseHash res;
			MD5((unsigned char*)lphrase.chars, lphrase.size_bytes - 1, res.c); //Size -1 because of the last space
			if (HASH_EQUALS(res, correctHash1)
				|| HASH_EQUALS(res, correctHash2)
				|| HASH_EQUALS(res, correctHash3)
				) {
				solutionMutex.lock();
				fout->open("solutions.txt", std::ios_base::app | std::ios_base::out);
				*fout << lphrase.chars << endl;
				fout->close();
				cout << "solution: " << lphrase.chars << endl;
				solutionMutex.unlock();
			};

		}
		else {
			tpStep(fout, lphrase, currentRes, lResChars, gresArray, resArraySizes, 0, nullptr, 1);
		}
	}
}

int main(size_t argc, char* argv[]) {
	//the new wordlist was filtered and ordered beforehand using node to remove impossible words
	ifstream file("newwordlist");
	if (file.is_open()) {
		size_t thread_num = 6;
		//optional threads argument
		if (argc > 1) {
			stringstream ss(argv[1]);
			ss >> thread_num;
		}
		//Fill inverted map
		for (unsigned char i = 0; i < 12; i++) {
			i_charMap[charMap.chars[i]] = i;
		}

		//Rwords from biggest to smallest
		RWord rWords[words_length] = { 0 };
		//Cwords from biggest to smallest
		CWord cWords[words_length];

		/*
		* Array containing arrays that contain CWords that are compatible with the resources specified by the index
		*/
		RWord** resArray = new RWord*[res_index_size];
		//Size of the arrays in resArray
		size_t* resArraySizes = new size_t[res_index_size];
		//fill rWords and cWords
		{
			string line("");
			size_t i = 0;

			while (file >> line) {
				char n;
				stringstream ss(line);
				size_t j = 0;
				cWords[i] = {
					0,
					{0}
				};
				while (ss >> n) {
					cWords[i].chars[j] = n;
					cWords[i].size_bytes++;
					rWords[i].chars[i_charMap[n]]++;
					rWords[i].cWord = &cWords[i];
					j++;
				}
				i++;
			}
			file.close();
		}
		//Fill resArray and resArraySizes
		{
			bool invalid;
			RWord word;
			unsigned int j;
			unsigned int k;
			char c;
			for (size_t i = 0; i < res_index_size; i++) {
				vector<RWord> rvec;
				//Inverse SERIALIZE_RES
				char inchars[12] = {
							LAST(i,1),
							MID(i,1,2),
							MID(i,2,3),
							MID(i,3,4),
							MID(i,4,5),
							MID(i,5,6),
							MID(i,6,7),
							MID(i,7,8),
							MID(i,8,10),
							MID(i,10,12),
							MID(i,12,14),
							MID(i,14,17)
				};
				for (j = 0; j < words_length; j++) {
					word = rWords[j];
					invalid = false;
					for (k = 0; k < 12; k++) {
						c = inchars[k];
						c -= word.chars[k];
						invalid = c < 0;
						if (invalid) break;
					}
					if (!invalid) {
						rvec.push_back(word);
					}
				}
				// O M G non freed naked pointers, oh wait these arrays are needed 
				// until the end of the program so they will be feed on program exit ;)
				RWord *rarr = new RWord[rvec.size()];
				copy(rvec.begin(), rvec.end(), rarr);
				resArray[i] = rarr;
				resArraySizes[i] = rvec.size();
			}
		}


		unsigned int initialRes = 76543; //SERIALIZE_RES(initialReschars)
		RWord initalResChars = {
			{
				1,
				1,
				1,
				1,
				1,
				1,
				1,
				1,
				2,
				2,
				2,
				4
			}
		};

		CPhrase initialPhrase = { {0},0 };
		size_t startIndex;
		string s;
		thread *threads = new thread[thread_num];
		ifstream status;

		//initialize threads
		for (int i = 0; i < thread_num; i++) {
			string *statusFileName = new string("status" + to_string(i) + ".txt");
			status.open(*statusFileName);
			if (status.fail()) {
				startIndex = i;
			}
			else {
				status >> s;
				status >> s;
				stringstream ss(s);
				ss >> startIndex;
			}
			status.close();
			ofstream* fout = new ofstream;
			threads[i] = thread(tpStep, fout, initialPhrase, initialRes, initalResChars, resArray, resArraySizes, startIndex, statusFileName, thread_num);
		}
		for (int i = 0; i < thread_num; i++) {
			threads[i].join();
		}
		cout << "done" << endl;
		cin.get();
	}
	else {
		cout << "no wordlist found" << endl;
	}
}
