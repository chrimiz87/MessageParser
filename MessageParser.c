//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// This code reads a message with a header and body format
// defined in an XML file, then writes to an output buffer
// in a different format from the same XML file.
//
// build the code with "gcc MessageParse.c -o ./mp"
//
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct xmlleaf{
  unsigned data;
  char * name;
} xmlleaf_t;

typedef struct xmlnode{
  // leaf values for this node
  int nleaves;
  xmlleaf_t * leaves;

  // other nodes that are children of this node
  int    nchildren;
  struct xmlnode * children;

  // the parent of this node
  struct xmlnode * parent;

  char * name;
} xmlnode_t;

typedef struct data{
  unsigned size;
  char * data;
  char * name;
} data_t;

void stripunwanted(char * line);
bool parseline(char * line, xmlnode_t ** currentptr);
void printxmltree(xmlnode_t * current);
void writemessage(char * input_buffer);
bool decodemessage(xmlnode_t * current, char ** input_buffer_ptr, char ** output_buffer_ptr);
bool decodeleaves(xmlnode_t * node, char ** input_buffer_ptr, data_t ** dataptr);
void freexmltree(xmlnode_t * current);

// this function reads the MessageStructure.xml to
// understand the structure of the messages the code will receive.
int main(){


  // open the Message Structure xml file for reading
  char * fname = "MessageStructure.xml";
  FILE * ptr = fopen(fname,"r");

  // check file was opened properly
  if(ptr == NULL){
    printf("Warning! %s could not be read", fname);
    return 1; 
  }

  // create the HEAD node of the xml tree
  xmlnode_t * current = NULL;
   //malloc( sizeof(xmlnode_t) );
  /* current->name      = "HEAD"; */
  /* current->nleaves   = 0; */
  /* current->leaves    = NULL; */
  /* current->nchildren = 0; */
  /* current->children  = NULL; */
  /* current->parent    = NULL; */

  // vars needed for reading file line-by-line
  char * line = NULL;
  size_t len = 0;
  ssize_t read;

  bool parse_success = true;
  
  // read file line-by-line
  while ((read = getline(&line, &len, ptr)) != -1) {

    // check for this line being a comment
    char * comment = strchr(line, '!');
    if(comment != NULL){
      // this line is a comment, skip it.
      continue;
    }

    // strip unwanted characters (space, tab, newline)
    stripunwanted(line);

    // extract information from the line
    parse_success &= parseline(line, &current);

    if(!parse_success){
      break;
    }
  }

  // close MessageStructure.xml
  fclose(ptr);
  
  if(!parse_success){
    printf("The parsing of MessageStructure.xml was not successful. Goodbye. \n");
    return 1;
  }

  // print xml tree (for debugging)
  printxmltree(current);
  printf("-----\n");

  ////////////////////////////////////////////////////////////////////////
  // We have now built the xml node tree from the xml file.
  // We will now write data to a buffer based on the message structure
  ////////////////////////////////////////////////////////////////////////

  // create large buffer for storing several messages.
  char * input_buffer = malloc(1024*sizeof(char) );

  // zero-initialise input buffer
  memset(input_buffer, '\0', 1024*sizeof(char) );

  // write message to the input buffer based on the message structure.
  writemessage(input_buffer);

  // create a large buffer for writing the output of the message decoding.
  char * output_buffer = malloc(1024*sizeof(char) );

  // define read and write pointers to the input and output buffers
  char * read_pointer = input_buffer;
  char * write_pointer = output_buffer;
  
  // decode message (based on the format specified in MessageStructure.xml)
  decodemessage(current, &read_pointer, &write_pointer);

  // free input and output buffers
  free(input_buffer);
  free(output_buffer);
  
  // delete the xml tree
  freexmltree(current);
  
  // finally, delete the HEAD node of the tree.
  free(current);

  // done
  return 0;
}

void stripunwanted(char * line){
  // this function removes whitespaces and newline (\n) from input lines

  unsigned idx=0 ;

  // remove leading whitespace, tab, newline
  while( line[idx] == ' ' || line[idx] == '\t' || line[idx] == '\n'){
    ++idx;
  }
  if(idx>0){
    int i=0;
    unsigned newsize = strlen(line)-idx;
    while(i < newsize){
      line[i] = line[idx+i];
      ++i;
    }
    line[newsize]='\0';
  }

  // remove trailing whitespace
  idx = strlen(line)-1;
  while( line[idx] == ' ' || line[idx] == '\t' || line[idx] == '\n'){
    --idx;
  }
  line[idx+1] = '\0';

  // remove any other whitespace, tabs, newlines
  for(idx=0; idx<strlen(line); ++idx){
    if(line[idx] == ' ' || line[idx] == '\t' || line[idx] == '\n'){ 
      for(int i=0; (idx+i)<strlen(line); ++i){
	// since we copy strlen(line),
	// the final character copied will be the zero termination
	line[idx+i] = line[idx+i+1];
      }
      --idx;
    }
  }
  
  return; 
}

bool parseline(char * line, xmlnode_t ** currentptr){

  xmlnode_t * current = *currentptr ;

  unsigned line_length = strlen(line);

  if(line_length==0 || line_length>99){
    printf("Warning. Line length is %u. This line will be skipped.", line_length);
    return false;
  }

  char * tag_start    = strchr(line, '<');
  char * tag_end      = strchr(line, '>');
  unsigned tag_length = tag_end - tag_start + 1 ;

  // tag_name string, zero initialised
  char tag_name[100];
  memset(tag_name, '\0', sizeof(tag_name));

  // grab the tag name,
  // without '<' and '>' by grabbing two characters less and starting at position +1
  strncpy(tag_name, tag_start+1, tag_length-2 );

  // there are 3 options:
  // 1. the token is 'opening' a new level to the heirarchy.
  //    - the only content on this line is the opening tag
  // 2. the token is 'closing' this level of the heirarchy.
  //    - the only content on this line is the closing tag
  // 3. there are opening and closing tags on the same line
  //    - this line is a 'leaf' node
  //    - the leaf content (int) is between the opening and closing tags

  // algorithm.
  // - Check if the token is the only thing on this line (options 1. and 2.)
  // by comparing the length of the line and the tokenname
  // - if yes, check if its an opening or closing tag by presence of '/', and
  //   act on the xml node tree accordingly.
  // - if not (option 3.) then grab the content and store in a leaf node.

  if( line_length == tag_length){
    
    if(strchr(tag_name, '/') == NULL){
      
      // Option 1. An opening tag. Create the head node,
      // or add a child layer to the xml node tree.
      
      bool head_node = false;
      
      // if its the first tag, create the head node
      if(current == NULL){
	head_node = true;
	current = malloc( sizeof(xmlnode_t) );
      }
      // if its the first child, malloc
      else if( current->nchildren == 0){
	
      	// increment number of children 
	++(current->nchildren);
	
	// alloc children array to store 1 child
	current->children = malloc( sizeof(xmlnode_t) );
      }
      // otherwise, realloc
      else{
	// increment number of children 
	++(current->nchildren);

	// realloc children array to store +1 children
	current->children = realloc( current->children,
				     current->nchildren * sizeof(xmlnode_t) );
      }

      // get pointer to new node
      xmlnode_t * temp = NULL;
      if(head_node){
	// point temp to the 'current' (head) node of the tree
	temp = current;

	// head nodes parent is NULL
	temp->parent = NULL;
      }
      else{
	// point temp to the new child in the tree
	temp = current->children+(current->nchildren-1);

	// set new nodes parent!
	temp->parent = (*currentptr);
      }

      // set the tag name for this node
      temp->name = malloc( 100*sizeof(char) );
      memset(temp->name, '\0', 100*sizeof(char) );
      strncpy(temp->name, tag_name, strlen(tag_name));

      // set current to this new node
      (*currentptr) = temp;      
    }
    else{      
      // Option 2. A closing tag. Set 'current' to its parent and do nothing else.
      // if the parent is NULL, this is the head node.
      if(current->parent != NULL){
	(*currentptr) = current->parent;
      }
    }
    
  }
  else{

    // Option 3. This line contains data that should be stored in a leaf
    // of the xml node tree.
    // The data to capture is an integer value between the first '>' symbol
    // and the second '<' symbol.
    // We already know the position of the first '>' symbol as 'tag_end'

    // find position of second '<' symbol:
    char * data_end = strchr( tag_end, '<' );

    if(data_end == NULL){
      printf("Warning. No second tag in this 'leaf' line. Skipping this line.\n");
      return false;
    }

    // number of characters between the end of the first tag and the
    // beginning of the second tag.
    unsigned data_length = data_end - tag_end + 1;


    char data_string[100];
    memset(data_string, '\0', sizeof(data_string));
    strncpy(data_string, tag_end+1, data_length-2);
    
    unsigned data = atoi(data_string);

    // now we have isolated the data, add it to a leaf node

    // first, increment number of leaves
    ++(current->nleaves);      

    // malloc or realloc a leaf for this data
    if(current->nleaves == 1){
      // first leaf, malloc
      current->leaves = malloc( sizeof(xmlleaf_t) );
    }
    // otherwise, realloc
    else{
      current->leaves = realloc( current->leaves,
				 current->nleaves * sizeof(xmlleaf_t) );
    }

    // get pointer to last leaf
    xmlleaf_t * temp = ((current->leaves)+(current->nleaves-1)); 
    
    // set the data of the new leaf (ugly syntax)
    temp->name = malloc( 100*sizeof(char) );
    memset(temp->name, '\0', 100*sizeof(char) );
    strncpy(temp->name, tag_name, strlen(tag_name));
    temp->data = data;
  }

  return true;
}

void printxmltree(xmlnode_t * current){

  printf("Node: %s \n", current->name);
  printf("Number of leaves: %u \n", current->nleaves);
  for(int i=0; i<current->nleaves; ++i){
    printf("Leaf %u : Name = %s : Data = %u \n", i, 
	   ((current->leaves)+i)->name,
	   ((current->leaves)+i)->data );
  }
  printf("Number of children: %u \n", current->nchildren);
  for(int i=0; i<current->nchildren; ++i){
    printxmltree( ((current->children)+i) );
  }

  return;
}

void writemessage(char * input_buffer){

  // this function fills the input buffer with a "message" of 26 bytes
  // it has the same format as the "message structure" so should be able
  // to be decoded based on the data format specified in MessageStructure.xml
  
  unsigned char typeID    = 0x01; // one byte
  unsigned char errorflag = 0x00; // one byte

  unsigned int data_1[4] = {0xBEEFBABE,1,2,3}; // 16 bytes
  unsigned int data_2    = 34952; // 4 bytes, will use 2 bytes
  unsigned int data_3    = 61166; // 4 bytes, will use 2 bytes

  /* printf("sizes are %lu %lu %lu %lu \n", */
  /* 	 sizeof(typeID), */
  /* 	 sizeof(errorflag), */
  /* 	 sizeof(data_1), */
  /* 	 sizeof(data_2) */
  /* 	 ); */
  
  unsigned offset = 0;
  memcpy( input_buffer+offset, &typeID,    sizeof(typeID));

  offset += sizeof(typeID);
  memcpy( input_buffer+offset, &errorflag, sizeof(errorflag));

  offset += sizeof(errorflag);
  memcpy( input_buffer+offset, &data_1,    sizeof(data_1));

  offset += sizeof(data_1);
  memcpy( input_buffer+offset, &data_2,    sizeof(data_2)/2);

  offset += sizeof(data_2)/2;
  memcpy( input_buffer+offset, &data_3,    sizeof(data_3)/2);

  offset += sizeof(data_3)/2;
  memset( input_buffer+offset, '\0', 1);

  printf("buffer length: %u \n", offset);
  for(int i=0; i<offset; ++i){
    printf("buffer char %u is %x \n", i, (unsigned char)(*(input_buffer+i)) );
  }
}

bool decodemessage(xmlnode_t * current, char ** input_buffer_ptr, char ** output_buffer_ptr){

  // decode the message in the input_buffer
  // and write it to the output buffer in the output message format.
  
  // check 'current' exists
  if(current == NULL){
    printf("Warning: tried to decode message but xml tree was not found.\n");
    return false;
  }

  // check 'current' is pointing to the head of the xml tree
  if(strcmp(current->name, "messageformats") != 0){
    printf("Warning: tried to decode message, but xml tree is not at");
    printf(" the 'messageformats' (head) node.\n");
    return false;
  }

  printf("current name %s, children %u, leaves %u \n",
	 current->name, current->nchildren, current->nleaves );

  // pointer to the input message format
  xmlnode_t * header = NULL;

  // all message formats contain 2 leaves, with message type ID and error flag.

  // find the header format:
  for(int i=0; i<current->nchildren; ++i){
    if( strcmp( (current->children+i)->name, "headerformat") == 0){
      header = current->children+i;
      break;
    }
  }
  
  printf("header name %s, children %u, leaves %u \n",
	 header->name, header->nchildren, header->nleaves );
  
  ///////////////////////////////////////////////////////////////
  // decode input_buffer 
  ///////////////////////////////////////////////////////////////

  // allocate space to store the header information
  data_t * header_data = malloc( header->nleaves * sizeof(data_t) );
  data_t * header_data_write_ptr = header_data;
  
  // decode header of the message
  decodeleaves(header, input_buffer_ptr, &header_data_write_ptr);

  // extract the message format ID and error flag from the header.
  // this is done with a loop to avoid changes to the header format
  // (e.g. additional leaves) from breaking the code.
  unsigned int message_format_ID = 0;
  bool has_message_format_ID = false;
  unsigned int error_flag     = 0;
  bool has_error_flag = false;
  for(int i=0; i<header->nleaves; ++i){
    if( strcmp(header_data[i].name, "messageformatID") == 0){
      memcpy( &message_format_ID, header_data[i].data, header_data[i].size);
      printf("message format ID %u \n", message_format_ID);
      has_message_format_ID = true;
    }
    else if( strcmp(header_data[i].name, "errorflag") == 0){
      memcpy( &error_flag, header_data[i].data, header_data[i].size);
      printf("error flag %u \n", error_flag);
      has_error_flag = true;
    }
    else{
      printf("header data named %s not processed.\n", header_data[i].name);
    }
  }

  // check for an error flag, or the error flag is missing
  if(has_error_flag == false || error_flag > 0){
    printf("Error flag raised: %u. Will not read message.\n", error_flag);
    return false;
  }

  if(has_message_format_ID == false){
    printf("Message format ID not read. Cannot read message.\n");
  }

  // this can only handle 9 message formats.
  char bodyformat[] = "bodyformatX";
  bodyformat[10] = 48+message_format_ID;

  xmlnode_t * body = NULL;
  
  // find the body format:
  for(int i=0; i<current->nchildren; ++i){
    if( strcmp( (current->children+i)->name, bodyformat) == 0){
      body = current->children+i;
      break;
    }
  }

  if(body == NULL){
    printf("did not find body format %s in the xml tree. Cannot decode message.\n", bodyformat);
    return false;
  }

  data_t * body_data = malloc( body->nleaves * sizeof(data_t) );
  data_t * body_data_write_ptr = body_data;
  
  // decode leaves of body format
  decodeleaves(body, input_buffer_ptr, &body_data_write_ptr);

  for(unsigned int i=0; i<body->nleaves; ++i){
    printf("message data %u is called %s and has size %u \n",
	   i, body_data[i].name, body_data[i].size);
    
    unsigned int read_size = 1;
    if(body_data[i].size/4>1){
      read_size = body_data[i].size/4;
    }
    printf("read size %u\n", read_size);

    for(unsigned int j=0; j<read_size; ++j){
      printf("\t %x \n", *(body_data[i].data+i) );
    }
    
  }
  
  return true;
}

bool decodeleaves(xmlnode_t * node, char ** input_buffer_ptr, data_t ** dataptr){
  
  // loop through leaves of this node
  for(int j=0; j<node->nleaves; ++j){
    
    // pointer to a leaf node 
    xmlleaf_t * leaf = node->leaves+j;

    printf("data size %u \n", leaf->data);
    
    // check for size of input data.
    if(leaf->data>=1024){
      printf("Warning. The data in %s has size %u and will not be read.", leaf->name, leaf->data);
      return false;
    }

    // print the message to the screen. For DEBUGGING

    // copy data from the input array to a temporary array
    /* unsigned int * tmp = malloc( leaf->data * sizeof(char)); */
    /* memcpy(tmp, *input_buffer_ptr, leaf->data); */

    /* // if the length is more than 4 bytes, requires multiple prints */
    /* unsigned int read_size = 1; */
    /* if(leaf->data/4 > 1){ */
    /*   read_size = leaf->data/4; */
    /* } */
    /* for(unsigned int i=0; i<read_size; ++i){ */
    /*   printf("\tdata in tmp buffer: %x \n", *(tmp+i) ); */
    /* } */
    /* free(tmp); */
    
    // write the message data to the data store
    
    (*dataptr)->name = leaf->name;
    (*dataptr)->size = leaf->data;
    (*dataptr)->data = malloc(sizeof(leaf->data));
    memcpy( (*dataptr)->data, *input_buffer_ptr, leaf->data);

    // increment the data store pointer.
    ++(*dataptr);

    // move the read buffer forwards by the appropriate amount.
    (*input_buffer_ptr) += leaf->data;
  }

  return true;
}
void freexmltree(xmlnode_t * current){

  // free all leaves of this node
  free(current->leaves);
  current->nleaves = 0;

  // call this function (recursive) on all children
  for(int i=0; i<current->nchildren; ++i){
    freexmltree( ((current->children)+i) );
  }
  
  // free the children
  free(current->children);
  current->nchildren = 0;
}
