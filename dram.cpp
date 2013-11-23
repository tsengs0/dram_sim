
#include <new>
#include <iostream>
#include <cstdio>
#include "dram.h"

using namespace std;

const char * const state_string_table[] = {
    "NULL",
    "IDLE",
    "ACTIVATING",
    "ACTIVE",
    "WRITE",
    "READ",
    "WRITEA",
    "READA",
    "PRE"
};


// =======================================================================================
// class dram_t::bank_t

dram_t::bank_t::bank_t() {}

// constructor
dram_t::bank_t::bank_t( dram_t *parent, int id ) {
    // DDR3-1333
    state = S_IDLE;
    cRCD = cCAS = cRAS = cRP = cRFC = 0;
    this->parent = parent;
    this->id = id;
}

void dram_t::bank_t::do_update() {
    DEC(cRCD);
    DEC(cCAS);
    DEC(cRAS);
    DEC(cRP);
    DEC(cRFC);
    if( state == S_ACTIVATING && cRCD == 0 ) {
        state = S_ACTIVE;
    }
    if( state == S_READ && parent->cCCD == 0 ) {
        state = S_ACTIVE;
    }
    if( state == S_WRITE && parent->cCCD == 0 ) {
        state = S_ACTIVE;
    }
    if( state == S_PRE && cRP == 0 ) {
        state = S_IDLE;
    }
}

void dram_t::bank_t::do_command() {
    if( parent->cmd.bank == id ) {
        if( !parent->is_issuable( parent->cmd ) ) {
            cout << "err: invalid - not issuable " << parent->cmd << endl;
        }
        int cmd = parent->cmd.cmd;
        if( cmd == CMD_ACTIVATE ) {
            if( state == S_IDLE ) {
                state = S_ACTIVATING;
                cRCD = parent->tRCD;
                cRAS = parent->tRAS;
                parent->cRRD = parent->tRRD;
                row = parent->cmd.addr;
            } else {
                throw "err: cmd==ACTIVATE but state!=IDLE";
            }
        } else if ( cmd == CMD_PRE ) {
            if( ( state == S_ACTIVE || state == S_READ || state == S_WRITE )
                && cRAS == 0 ) {
                state = S_PRE;
                cRP = parent->tRP;
            } else {
                throw "err: cmd==PRE but state!=ACTIVE,READ,WRITE";
            }
        } else if ( cmd == CMD_READ ) {
            if( state == S_ACTIVE && parent->cCCD == 0 ) {
                state = S_READ;
                parent->cCCD = parent->tCCD;
                cCAS = parent->tCAS;
            } else {
                throw "err: cmd==READ, conditions not met";
            }
        } else if ( cmd == CMD_WRITE ) {
            if( state==S_ACTIVE && parent->cCCD==0 ) {
                state = S_WRITE;
                parent->cCCD = parent->tCCD;
                cCAS = parent->tCAS;
            } else {
                throw "err: cmd==WRITE, conditions not met";
            }
        }
    }

    // debug
    //printf( "bank[%d] state=%s cRCD=%d cCAS=%d cRAS=%d cRP=%d\n", id, state_string_table[state], cRCD, cCAS, cRAS, cRP );
}


// =======================================================================================
// class dram_t

// constructor
dram_t::dram_t()
{
    width  = 8;
    ncols  = 1024;
    nrows  = 8192;
    nbanks = 8;
    burst  = 8;

    // アドレス計算フィルタを初期化
    const int burst_bits = needed_bits(burst);
    int lsb=burst_bits;
    bank_addr = addr_part( needed_bits(nbanks), lsb, "bank" );
    lsb += needed_bits(nbanks);
    // バースト長の分(3bits)を引く
    col_addr  = addr_part( needed_bits(ncols)-burst_bits , lsb, "col" );
    lsb += needed_bits(ncols) - burst_bits;
    row_addr  = addr_part( needed_bits(nrows) , lsb, "row" );

    tCAS = 9;
    tRCD = 9;
    tRAS = 24;
    tRP  = 9;
    tRC  = tRAS + tRP;
    tRFC = 74;
    tCCD = 4;
    tRRD = 4;
    
    cRRD = cCCD = 0;
    bank = new bank_t[nbanks];
    for( int i=0; i<nbanks; ++i ) {
        // placement new
        new(&bank[i]) bank_t( this, i );
    }
}

dram_t::~dram_t() {
    delete bank;
}

// コマンド発行条件を満たしているかをチェックする
bool dram_t::is_issuable( const cmd_t& command ) const {
    const int cmd = command.cmd;

    // グローバルなコマンド
    if( cmd == CMD_NOP ) {
        // 無条件でok
        return true;
    }

    // バンクローカルなコマンド
    const int bankid = command.bank;
    const int state = bank[bankid].state;
    if( cmd == CMD_ACTIVATE ) {
        if( state == S_IDLE ) {
            return true;
        } else {
            return false;
        }
    } else if ( cmd == CMD_PRE ) {
        if( state == S_ACTIVE || state == S_READ || state == S_WRITE
            && bank[bankid].cRAS == 0 ) {
            return true;
        } else {
            return false;
        }
    } else if ( cmd == CMD_READ ) {
        if( state == S_ACTIVE && cCCD == 0 ) {
            return true;
        } else {
            return false;
        }
    } else if ( cmd == CMD_WRITE ) {
        if( state==S_ACTIVE && cCCD==0 ) {
            return true;
        } else {
            return false;
        }
    }

    // invalid command number
    printf( "dram_t::is_issuable(): invalid command number = %d\n", cmd );
    return false;
}

void dram_t::do_update() {
    DEC(cRRD);
    DEC(cCCD);
    for( int bankid=0; bankid<nbanks; ++bankid ) {
        bank[bankid].do_update();
    }
}

void dram_t::do_command() {
    // debug print
    //printf( "dram: cRRD=%d, cCCD=%d\n", cRRD, cCCD );
    if( cmd.cmd != CMD_NOP )
        cout << "dram: cmd=" << cmd << endl;

    // グローバルなコマンド

    // バンクローカルなコマンド
    if( cmd.cmd!=CMD_NOP && is_issuable( cmd ) ) {
        bank[cmd.bank].do_command();
    }

    // コマンドを無効化
    cmd.cmd = CMD_NOP;
}


// =======================================================================================
// class dram_req_t

dram_req_t::dram_req_t( int rw, int bankid, int row, int col, mem_req_t* from )
    : rw(rw), bank(bankid), row(row), col(col), from(from)
{
}

// =======================================================================================
// class schedule_t

schedule_t::schedule_t( int type, int count, dram_req_t& req )
    : type(type), count(count), req(req)
{
}

// =======================================================================================
// class dram_controller

// constructor
dram_controller::dram_controller() {

}

void dram_controller::cycle1() {
    // dram の内部更新処理
    dram.do_update();

    // ポートにリクエストが来ているか確認
    if( port.in.data.valid ) {
        printf( "dram_controller: request came\n" );
        printf( "rw=%d addr=0x%x length=%d\n", port.in.data.rw, port.in.data.addr, port.in.data.length );
        // 終了待ち行列に入れる
        queue.push_back( port.in.data );
        mem_req_t &req = queue[queue.size()-1];
        unsigned int &addr = port.in.data.addr;
        printf( "=> row=%u, col=%u, bank=%u\n",
                dram.row_addr.get(addr), dram.col_addr.get(addr), dram.bank_addr.get(addr) );
        // アラインメントチェック
        if( ( addr & (dram.width * dram.burst / 8 - 1) ) != 0 ) {
            printf( "controller: ERR invalid alignment\n" );
            port.out.data = port.in.data;
            port.out.data.err = 1;
            queue.pop_back();
        } else {
            for( unsigned int offset=0; offset<req.length; offset+=(dram.width*dram.burst/8) ) {
                unsigned int a = addr + offset;
                int bankid = dram.bank_addr.get(a);
                // push a request into a bank queue
                bankq[bankid].push_back( dram_req_t( req.rw, bankid, dram.row_addr.get(a), dram.col_addr.get(a), &req ) );
                req.count++;
                cout << "req.count = " << req.count << endl;
                printf("push into a bank queue. bankid=%d row=%d col=%d\n", bankid, dram.row_addr.get(a), dram.col_addr.get(a) );
            }
        }
    }

    for( vector<schedule_t>::iterator it=schedules.begin(); it!=schedules.end(); ) {
        (*it).count -= 1;
        cout << "count " << dec << (*it).count << endl;
        if( (*it).count == 0 ) {
            (*it).req.from->count -= 1;
            cout<<"req.count = "<<(*it).req.from->count<<endl;
            it = schedules.erase( it );
        } else {
            ++it;
        }
    }

    for( deque<mem_req_t>::iterator it=queue.begin(); it!=queue.end(); ++it ) {
        if( (*it).count == 0 ) {
            port.out.data = *it;
            queue.erase( it );
            break;
        }
    }

    // バンク・リクエスト・キューに基づいてDRAMにコマンドを発行する
    // 注意: 1サイクルで発行できるコマンドは1個!
    for( int bankid=0; bankid<dram.nbanks; ++bankid ) {
        if( bankq[bankid].size() > 0 ) {
            dram_t::bank_t &b = dram.bank[bankid];
            dram_req_t &req = bankq[bankid][0];
            cmd_t cmd;
            // アクティブか？
            if( b.state == S_IDLE ) {
                cmd.cmd = CMD_ACTIVATE;
                cmd.bank = bankid;
            } else if( b.state == S_ACTIVE && b.row == req.row ) {
                cmd.cmd = CMD_READ;
                cmd.bank = bankid;
                cmd.addr = req.col;
            } else if( b.state == S_ACTIVE && b.row != req.row ) {
                cmd.cmd = CMD_PRE;
            }
            // コマンドを発行できるかチェック
            if( dram.is_issuable( cmd ) ) {
                dram.cmd = cmd;
                if( cmd.cmd == CMD_READ ) {
                    schedules.push_back( schedule_t( 0, dram.tCAS+dram.burst/2, req ) );
                    cout << "bank " << bankid << ": " << cmd << endl;
                    cout << "  data transfer will be completed in " << dec << dram.tCAS+dram.burst/2 << " cycles" << endl;
                    bankq[bankid].pop_front();
                }
                break;
            }
        } else {
            // バンクに対するリクエストがない場合はプリチャージを試みる
            /*
            cmd_t cmd( CMD_PRE, bankid, 0 );
            if( dram.is_issuable( cmd ) ) {
                dram.cmd = cmd;
            }
            */
        }
    }

    // dram のコマンド処理
    dram.do_command();

    // debug print
    printf( "dram: cRRD=%d, cCCD=%d\n", dram.cRRD, dram.cCCD );
    for( int bankid=0; bankid<dram.nbanks; ++bankid ) {
        dram_t::bank_t& b = dram.bank[bankid];
        printf( "bank[%d]: state=%-10s, cRCD=%d cCAS=%d, cRAS=%2d, cRP=%d\n", bankid, state_string_table[b.state], b.cRCD, b.cCAS, b.cRAS, b.cRP );
    }
}

void dram_controller::cycle2() {
}

// =======================================================================================
// class cmd_t

cmd_t::cmd_t()
    : cmd(CMD_NOP), bank(0), addr(0)
{
}

cmd_t::cmd_t( int cmd, int bank, unsigned int addr )
    : cmd(cmd), bank(bank), addr(addr)
{
}

static const char * const cmd_string_table[] = {
    "NOP",
    "ACTIVATE",
    "READ",
    "READA",
    "WRITE",
    "WRITEA",
    "PRE"
};

void cmd_t::print() const {
    printf( "[cmd=%s, bank=%d, addr=0x%x]", cmd_string_table[cmd], bank, addr );
}

std::ostream& operator <<( std::ostream& os, const cmd_t& value ) {
    value.print();
    return os;
}

