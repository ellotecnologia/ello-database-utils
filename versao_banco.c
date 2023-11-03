#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <ibase.h>

#define ERREXIT(status, rc)     {isc_print_status(status); return rc;}
#define SQL_VARCHAR(len) struct {short vary_length; char vary_string[(len)+1];}
#define BUFFER_LENGTH 32

typedef struct {
    isc_db_handle db;
    isc_tr_handle trans;
} FBConnection;

FBConnection* fb_connect(const char* database) {
    ISC_STATUS_ARRAY status;
    FBConnection *conn = malloc(sizeof(FBConnection));
    conn->db = NULL;
    conn->trans = NULL;
    if(isc_attach_database(status, 0, database, &conn->db, 0, NULL)) {
        ERREXIT(status, (FBConnection*)NULL);
    }
    if(isc_start_transaction(status, &conn->trans, 1, &conn->db, 0, NULL)) {
        ERREXIT(status, (FBConnection*)NULL);
    }
    return conn;
}

void fb_close(FBConnection *conn)
{
    ISC_STATUS_ARRAY status;
    isc_commit_transaction(status, &conn->trans);
    isc_detach_database(status, &conn->db);
    free(conn);
}

int query(FBConnection* conn, const char* select, char* version)
{
    isc_stmt_handle stmt = NULL;
    
    ISC_STATUS_ARRAY status;
    XSQLDA *sqlda;
    
    SQL_VARCHAR(BUFFER_LENGTH) versao;
    short flag0 = 0;
    long fetch_stat;

    sqlda = (XSQLDA *) malloc(XSQLDA_LENGTH(1));
    sqlda->sqln = 1;
    sqlda->sqld = 1;
    sqlda->version = 1;
    
    if(isc_dsql_allocate_statement(status, &conn->db, &stmt)) {
        ERREXIT(status, 1);
    }
    
    if(isc_dsql_prepare(status, &conn->trans, &stmt, 0, select, 1, sqlda)) {
        ERREXIT(status, 1);
    }

    sqlda->sqlvar[0].sqldata = (char*)&versao;
    sqlda->sqlvar[0].sqltype = SQL_VARYING + 1;
    sqlda->sqlvar[0].sqlind = &flag0;
    
    if(isc_dsql_execute(status, &conn->trans, &stmt, 1, NULL)) {
        ERREXIT(status, 1);
    }
    
    fetch_stat = isc_dsql_fetch(status, &stmt, 1, sqlda);
        
    if(fetch_stat != 0) {
        ERREXIT(status, 1);
    }
    
    strncpy(version, versao.vary_string, versao.vary_length);
    
    isc_dsql_free_statement(status, &stmt, DSQL_close);
    
    return 0;
}

int main(int argc, char* argv[])
{
    char version[BUFFER_LENGTH] = {0};
    char patch_num[BUFFER_LENGTH] = {0};
    char text[64];
    
    FBConnection* conn = fb_connect(argv[1]);
    query(conn, "SELECT CAST(Valor AS VARCHAR(32)) FROM TGerParametros WHERE parametro='GERVERSAODORELEASE'", version);
    query(conn, "SELECT CAST(Valor AS VARCHAR(32)) FROM TGerParametros WHERE parametro='GERIDSCRIPTRELEASE'", patch_num);
    fb_close(conn);
    
    sprintf(text, "Versão %s Patch %s", version, patch_num);
    
    MessageBoxA(NULL, text, "Versão do Banco de Dados", MB_OK);

    return 0;
}
