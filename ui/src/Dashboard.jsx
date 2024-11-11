import React, { useEffect, useState } from 'react';
import SearchBox from './SearchBox.jsx';
import './Dashboard.scss';
import SaveAltIcon from '@mui/icons-material/SaveAlt';
import Button from "@mui/material/Button";
import {useNavigate} from "react-router-dom";

export default function Dashboard() {
    const [status, setStatus] = useState({ ready: false });
    const navigate = useNavigate();
    let cachedNow;
    const updateStatus = (now) => {
        if (cachedNow !== now) {
            cachedNow = now;
            appAPI.status(now).then((s) => {
                setStatus(s);
            });
        }
    };
    const doExport = async () => {
        const selectRes = await appAPI.selectDir();
        console.log('selectRes:', selectRes);
        if (!selectRes?.canceled && Array.isArray(selectRes?.filePaths) && selectRes?.filePaths.length)
            navigate('/export/' + encodeURIComponent(selectRes.filePaths[0]));
    };
    useEffect(() => {
        updateStatus(Date.now());
    }, []);
    if (status?.ready) {
        return <div className="content">
            <div className="dashboard">
                <div className="col">
                    <div className="row">
                        <div>
                            <img className="logo-large" src="static/logo.svg" />
                        </div>
                    </div>
                    <div className="row">
                        <div className="col">
                            <h3 className="white">{status?.lastBlock?.timestamp}</h3>
                            <p className="note">the generation time of the last synchronized block</p>
                        </div>
                    </div>
                    <div className="search">
                        <SearchBox />
                    </div>
                    <div className="row">
                        <div className="col">
                            <h4 className="hash">{status?.lastBlock?.hash}</h4>
                            <p className="note">the hash of the last synchronized block</p>
                        </div>
                    </div>
                    <div className="row secondary">
                        <div className="col">
                            <h4>{status?.lastBlock?.slot}</h4>
                            <p className="note">its slot</p>
                        </div>
                        <div className="col">
                            <h4>{status?.lastBlock?.epoch}</h4>
                            <p className="note">its epoch</p>
                        </div>
                        <div className="col">
                            <h4>{status?.lastBlock?.epochSlot}</h4>
                            <p className="note">its epoch's slot</p>
                        </div>
                    </div>
                    <div className="row secondary">
                        <Button className="search-button" startIcon={<SaveAltIcon />}
                            variant="contained" color="secondary" size="large" onClick={doExport}
                            disabled={false && !status?.exportable}>Export State to Daedalus
                        </Button>
                    </div>
                </div>
            </div>
        </div>;
    } else {
        return <>
            <h1>Sorry, but I couldn't synchronize the blockchain.</h1>
            <p>
                Please, check your Internet connection and restart the app
            </p>
        </>;
    }
}