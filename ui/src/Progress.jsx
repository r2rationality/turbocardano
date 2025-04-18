import React from 'react';
import LinearProgress from '@mui/material/LinearProgress';
import PropTypes from 'prop-types';
import Typography from '@mui/material/Typography';
import Box from '@mui/material/Box';
import HardwareStatus from './HardwareStatus.jsx';
import './Progress.scss';

function LinearProgressWithLabel(props) {
    return (
        <Box sx={{ display: 'flex', alignItems: 'center' }}>
            <Box sx={{ width: '100%', mr: 1 }}>
                <LinearProgress variant="determinate" {...props} />
            </Box>
            <Box sx={{ minWidth: 35 }}>
                <Typography variant="body2" color="text.secondary">{`${parseFloat(props.value).toFixed(3)}%`}</Typography>
            </Box>
        </Box>
    );
}
  
LinearProgressWithLabel.propTypes = {
    value: PropTypes.number.isRequired,
};

export default function Progress({ progress, names, weights, titles, hardware, duration, eta, slotRange }) {
    let durationInfo = <></>;
    if (duration) {
        durationInfo = <div className="duration">
            <div className="time-info eta">
                <div className="note">remaining time</div>
                <h2>{eta ? eta >= 1 ? eta + " min" : "< 1 min" : "estimating"}</h2>
            </div>
            <div className="time-info">
                <div className="note">run time</div>
                <h2>{duration} min</h2>
            </div>
        </div>;
    }
    const normWeights = weights ?? Object.fromEntries(names.map(name => [name, 1.0 / names.length]));
    const normTitles = titles ?? Object.fromEntries(names.map(name => [name, name]));
    const myProgress = Object.fromEntries(names.map(name => [name, progress[name] ?? "0.000%"]));
    const numItems = Object.keys(myProgress).length;
    const progressItems = Object.entries(myProgress).map((entry, idx) =>
        <div className="progress-item">
            <div className="title">{normTitles[entry[0]]}</div>
            <div className="indicator">
                <LinearProgressWithLabel color="primary" variant="determinate" value={entry[1].slice(0, -1)} />
            </div>
        </div>
    );
    const progressDetails = <>
        {progressItems}
    </>;
    let totalProgress = 100;
    if (numItems > 0) {
        totalProgress = 0;
        for (let [k, v] of Object.entries(myProgress)) {
            totalProgress += parseFloat(v?.slice(0, -1)) * normWeights[k];
        }
    }
    const slotRangeDiv = slotRange
        ?   <div className="slot-range">
            <div className="start">{slotRange.start}</div>
            <div className="target">{slotRange.target}</div>
        </div>
        :  <></>;
    return <div className="progress">
        {durationInfo}
        <div className="progress-item total">
            <div className="title">total</div>
            <div className="indicator">
                <LinearProgressWithLabel color="primary" variant="determinate" value={totalProgress} />
            </div>
            {slotRangeDiv}
        </div>
        {progressDetails}
        <HardwareStatus hardware={hardware} />
    </div>;
}